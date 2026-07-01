#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "hiim/hub/hub_server.hpp"
#include "hiim/wire/auth.hpp"
#include "hiim/wire/header.hpp"
#include "hiim/wire/sys_cmd.hpp"
#include "wire/frame_buffer.hpp"

namespace {

using hiim::wire::EncodeAuthFrame;
using hiim::wire::EncodeFrame;
using hiim::wire::FrameBuffer;
using hiim::wire::HeaderFieldHost;
using hiim::wire::HostToBe32;
using hiim::wire::kFlagExp;
using hiim::wire::kFlagSys;
using hiim::wire::SysCmd;
using hiim::wire::WireHeader;

#define EXPECT_TRUE(expr)                                                     \
  do {                                                                        \
    if (!(expr)) {                                                            \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << " " #expr "\n";  \
      return 1;                                                               \
    }                                                                         \
  } while (0)

int ConnectLocalhost(int port) {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(static_cast<uint16_t>(port));
  inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);
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
    if (n <= 0) return false;
    off += static_cast<std::size_t>(n);
  }
  return true;
}

bool WaitForFrame(int fd, FrameBuffer& fb, uint32_t expect_type) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto frame = fb.TryPopFrame()) {
      return HeaderFieldHost(frame->header, &WireHeader::type) == expect_type;
    }
    uint8_t buf[4096];
    const ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
      fb.Append(std::span<const uint8_t>(buf, static_cast<std::size_t>(n)));
    }
  }
  return false;
}

int TestBridgeEndToEnd() {
  hiim::hub::DualHubConfig cfg;
  cfg.forward_listen = "127.0.0.1:29888";
  cfg.backend_listen = "127.0.0.1:29889";
  cfg.health_listen = "127.0.0.1:28080";
  cfg.reactor_threads = 1;
  cfg.worker_threads = 1;

  hiim::hub::HubServer hub(cfg);
  EXPECT_TRUE(hub.Start());

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const int backend_fd = ConnectLocalhost(29889);
  EXPECT_TRUE(backend_fd >= 0);
  const auto backend_auth = EncodeAuthFrame(1, "proxy", "proxy", 30001);
  EXPECT_TRUE(WriteAll(backend_fd, backend_auth));
  FrameBuffer backend_fb;
  EXPECT_TRUE(WaitForFrame(backend_fd, backend_fb,
                           static_cast<uint32_t>(SysCmd::kAuthAck)));

  const uint32_t cmd = 0x030B;
  const uint32_t cmd_be = HostToBe32(cmd);
  const auto sub = EncodeFrame(static_cast<uint32_t>(SysCmd::kSubReq), 30001, kFlagSys,
                               std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&cmd_be),
                                                        sizeof(cmd_be)));
  EXPECT_TRUE(WriteAll(backend_fd, sub));
  EXPECT_TRUE(WaitForFrame(backend_fd, backend_fb, static_cast<uint32_t>(SysCmd::kSubAck)));

  const int forward_fd = ConnectLocalhost(29888);
  EXPECT_TRUE(forward_fd >= 0);
  const auto forward_auth = EncodeAuthFrame(1, "proxy", "proxy", 20001);
  EXPECT_TRUE(WriteAll(forward_fd, forward_auth));
  FrameBuffer forward_fb;
  EXPECT_TRUE(WaitForFrame(forward_fd, forward_fb,
                           static_cast<uint32_t>(SysCmd::kAuthAck)));

  std::vector<uint8_t> im_payload(48, 0);
  const uint32_t type_be = HostToBe32(cmd);
  const uint32_t dest_be = HostToBe32(30001);
  std::memcpy(im_payload.data(), &type_be, sizeof(type_be));
  std::memcpy(im_payload.data() + 4, &dest_be, sizeof(dest_be));

  const auto biz = EncodeFrame(cmd, 20001, kFlagExp, im_payload);
  EXPECT_TRUE(WriteAll(forward_fd, biz));

  EXPECT_TRUE(WaitForFrame(backend_fd, backend_fb, cmd));

  close(forward_fd);
  close(backend_fd);
  hub.Stop();
  hub.Wait();
  return 0;
}

}  // namespace

int main() {
  if (TestBridgeEndToEnd() != 0) {
    return 1;
  }
  std::cout << "hub_integration_test: OK\n";
  return 0;
}
