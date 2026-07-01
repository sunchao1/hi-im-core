#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "hiim/wire/auth.hpp"
#include "hiim/wire/header.hpp"
#include "hiim/wire/sys_cmd.hpp"
#include "wire/frame_buffer.hpp"

namespace {

using hiim::wire::DecodeHeader;
using hiim::wire::EncodeAuthFrame;
using hiim::wire::EncodeFrame;
using hiim::wire::FrameBuffer;
using hiim::wire::HeaderFieldHost;
using hiim::wire::HostToBe32;
using hiim::wire::kFlagExp;
using hiim::wire::kFlagSys;
using hiim::wire::SysCmd;
using hiim::wire::WireHeader;

struct Options {
  std::string forward_addr{"127.0.0.1:28888"};
  std::string backend_addr{"127.0.0.1:28889"};
  std::string mode{"publish"};
  std::chrono::seconds duration{10};
  std::size_t payload_size{256};
  int publishers{4};
  uint32_t cmd{0x030B};
  std::string auth_user{"proxy"};
  std::string auth_pass{"proxy"};
};

Options ParseArgs(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() {
      if (i + 1 < argc) return std::string(argv[++i]);
      return std::string{};
    };
    if (arg == "-mode" || arg == "--mode") {
      opt.mode = next();
    } else if (arg == "-duration" || arg == "--duration") {
      const auto s = next();
      if (!s.empty() && s.back() == 's') {
        opt.duration = std::chrono::seconds(std::stoi(s.substr(0, s.size() - 1)));
      } else {
        opt.duration = std::chrono::seconds(std::stoi(s));
      }
    } else if (arg == "-payload" || arg == "--payload") {
      opt.payload_size = static_cast<std::size_t>(std::stoul(next()));
    } else if (arg == "-publishers" || arg == "--publishers") {
      opt.publishers = std::stoi(next());
    } else if (arg == "-forward" || arg == "--forward") {
      opt.forward_addr = next();
    } else if (arg == "-backend" || arg == "--backend") {
      opt.backend_addr = next();
    } else if (arg == "-cmd" || arg == "--cmd") {
      opt.cmd = static_cast<uint32_t>(std::stoul(next(), nullptr, 0));
    } else if (arg == "--auth-user") {
      opt.auth_user = next();
    } else if (arg == "--auth-pass") {
      opt.auth_pass = next();
    }
  }
  return opt;
}

bool ParseHostPort(const std::string& addr, std::string& host, int& port) {
  const auto colon = addr.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  host = addr.substr(0, colon);
  port = std::stoi(addr.substr(colon + 1));
  return true;
}

int ConnectTcp(const std::string& addr) {
  std::string host;
  int port = 0;
  if (!ParseHostPort(addr, host, port)) {
    return -1;
  }
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, host.c_str(), &sa.sin_addr);
  if (connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

bool WriteAll(int fd, std::span<const uint8_t> data) {
  std::size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = send(fd, data.data() + off, data.size() - off, 0);
    if (n <= 0) {
      return false;
    }
    off += static_cast<std::size_t>(n);
  }
  return true;
}

bool ReadFrame(int fd, FrameBuffer& fb, hiim::wire::FrameView& out) {
  uint8_t buf[8192];
  while (true) {
    if (auto frame = fb.TryPopFrame()) {
      out = *frame;
      return true;
    }
    const ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      return false;
    }
    fb.Append(std::span<const uint8_t>(buf, static_cast<std::size_t>(n)));
  }
}

bool Handshake(int fd, uint32_t nid, const Options& opt, uint32_t sub_cmd) {
  const auto auth = EncodeAuthFrame(1, opt.auth_user, opt.auth_pass, nid);
  if (!WriteAll(fd, auth)) {
    return false;
  }

  FrameBuffer fb;
  hiim::wire::FrameView frame{};
  if (!ReadFrame(fd, fb, frame)) {
    return false;
  }
  if (HeaderFieldHost(frame.header, &WireHeader::type) !=
      static_cast<uint32_t>(SysCmd::kAuthAck)) {
    return false;
  }

  if (sub_cmd != 0) {
    const uint32_t cmd_be = HostToBe32(sub_cmd);
    const auto sub = EncodeFrame(static_cast<uint32_t>(SysCmd::kSubReq), nid, kFlagSys,
                                 std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&cmd_be),
                                                          sizeof(cmd_be)));
    if (!WriteAll(fd, sub)) {
      return false;
    }
    if (!ReadFrame(fd, fb, frame)) {
      return false;
    }
    if (HeaderFieldHost(frame.header, &WireHeader::type) !=
        static_cast<uint32_t>(SysCmd::kSubAck)) {
      return false;
    }
  }
  return true;
}

std::vector<uint8_t> MakeBusinessPayload(std::size_t payload_size, uint32_t dest_nid) {
  std::vector<uint8_t> payload(std::max(payload_size, std::size_t(48)), 0);
  const uint32_t type_be = HostToBe32(0x030B);
  const uint32_t nid_be = HostToBe32(dest_nid);
  std::memcpy(payload.data(), &type_be, sizeof(type_be));
  std::memcpy(payload.data() + 4, &nid_be, sizeof(nid_be));
  return payload;
}

struct BenchResult {
  uint64_t sent{0};
  uint64_t recv{0};
};

BenchResult RunPublishBench(const Options& opt) {
  std::atomic<bool> running{true};
  std::atomic<uint64_t> recv_count{0};
  std::atomic<uint64_t> sent_count{0};

  std::thread subscriber([&] {
    const int fd = ConnectTcp(opt.backend_addr);
    if (fd < 0) {
      running.store(false);
      return;
    }
    if (!Handshake(fd, 30001, opt, opt.cmd)) {
      close(fd);
      running.store(false);
      return;
    }

    FrameBuffer fb;
    const auto end = std::chrono::steady_clock::now() + opt.duration;
    while (std::chrono::steady_clock::now() < end &&
           running.load(std::memory_order_relaxed)) {
      uint8_t buf[8192];
      const ssize_t n = recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) {
        break;
      }
      fb.Append(std::span<const uint8_t>(buf, static_cast<std::size_t>(n)));
      while (auto frame = fb.TryPopFrame()) {
        if (HeaderFieldHost(frame->header, &WireHeader::flag) == kFlagExp) {
          recv_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
    close(fd);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::vector<std::thread> publishers;
  for (int i = 0; i < opt.publishers; ++i) {
    publishers.emplace_back([&, i] {
      const int fd = ConnectTcp(opt.forward_addr);
      if (fd < 0) {
        running.store(false);
        return;
      }
      const uint32_t nid = static_cast<uint32_t>(20001 + i);
      if (!Handshake(fd, nid, opt, 0)) {
        close(fd);
        running.store(false);
        return;
      }

      const auto payload = MakeBusinessPayload(opt.payload_size, 30001);
      const auto end = std::chrono::steady_clock::now() + opt.duration;
      while (std::chrono::steady_clock::now() < end &&
             running.load(std::memory_order_relaxed)) {
        const auto frame = EncodeFrame(opt.cmd, nid, kFlagExp, payload);
        if (!WriteAll(fd, frame)) {
          break;
        }
        sent_count.fetch_add(1, std::memory_order_relaxed);
      }
      close(fd);
    });
  }

  for (auto& t : publishers) {
    t.join();
  }
  subscriber.join();

  return BenchResult{sent_count.load(), recv_count.load()};
}

BenchResult RunUnicastBench(const Options& opt) {
  std::atomic<bool> running{true};
  std::atomic<uint64_t> recv_count{0};
  std::atomic<uint64_t> sent_count{0};

  std::thread receiver([&] {
    const int fd = ConnectTcp(opt.forward_addr);
    if (fd < 0) {
      running.store(false);
      return;
    }
    if (!Handshake(fd, 20002, opt, opt.cmd)) {
      close(fd);
      running.store(false);
      return;
    }

    FrameBuffer fb;
    const auto end = std::chrono::steady_clock::now() + opt.duration;
    while (std::chrono::steady_clock::now() < end) {
      uint8_t buf[8192];
      const ssize_t n = recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) break;
      fb.Append(std::span<const uint8_t>(buf, static_cast<std::size_t>(n)));
      while (auto frame = fb.TryPopFrame()) {
        if (HeaderFieldHost(frame->header, &WireHeader::flag) == kFlagExp) {
          recv_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
    close(fd);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const int fd = ConnectTcp(opt.backend_addr);
  if (fd >= 0) {
    if (Handshake(fd, 30002, opt, 0)) {
      const auto payload = MakeBusinessPayload(opt.payload_size, 20002);
      const auto end = std::chrono::steady_clock::now() + opt.duration;
      while (std::chrono::steady_clock::now() < end) {
        const auto frame = EncodeFrame(opt.cmd, 30002, kFlagExp, payload);
        if (!WriteAll(fd, frame)) break;
        sent_count.fetch_add(1, std::memory_order_relaxed);
      }
    }
    close(fd);
  }

  receiver.join();
  return BenchResult{sent_count.load(), recv_count.load()};
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = ParseArgs(argc, argv);
  const auto started = std::chrono::steady_clock::now();

  BenchResult result{};
  if (opt.mode == "unicast") {
    result = RunUnicastBench(opt);
  } else {
    result = RunPublishBench(opt);
  }

  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  const double sent_per_sec = seconds > 0 ? static_cast<double>(result.sent) / seconds : 0.0;
  const double recv_per_sec = seconds > 0 ? static_cast<double>(result.recv) / seconds : 0.0;

  std::cout << "mode=" << opt.mode << " duration=" << opt.duration.count() << "s"
            << " payload=" << opt.payload_size << "B"
            << " sent=" << result.sent << " recv=" << result.recv << "\n"
            << "sent/s=" << static_cast<uint64_t>(sent_per_sec)
            << " recv/s=" << static_cast<uint64_t>(recv_per_sec) << "\n";

  return 0;
}
