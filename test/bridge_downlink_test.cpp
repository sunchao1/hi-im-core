// Copyright 2026 chao.sun
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "hiim/hub/hub_server.hpp"
#include "hiim/im/header.hpp"
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
    if (n <= 0) {
      return false;
    }
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

bool AuthAndSub(int fd, uint32_t nid, FrameBuffer& fb) {
  const auto auth = EncodeAuthFrame(1, "proxy", "proxy", nid);
  if (!WriteAll(fd, auth)) {
    return false;
  }
  if (!WaitForFrame(fd, fb, static_cast<uint32_t>(SysCmd::kAuthAck))) {
    return false;
  }
  const uint32_t cmd = 0x030B;
  const uint32_t cmd_be = HostToBe32(cmd);
  const auto sub = EncodeFrame(static_cast<uint32_t>(SysCmd::kSubReq), nid, kFlagSys,
                               std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&cmd_be),
                                                        sizeof(cmd_be)));
  if (!WriteAll(fd, sub)) {
    return false;
  }
  return WaitForFrame(fd, fb, static_cast<uint32_t>(SysCmd::kSubAck));
}

bool DrainForMs(int fd, FrameBuffer& fb, int ms) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < deadline) {
    uint8_t buf[4096];
    const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n > 0) {
      fb.Append(std::span<const uint8_t>(buf, static_cast<std::size_t>(n)));
      if (fb.TryPopFrame()) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return fb.TryPopFrame().has_value();
}

std::optional<std::string> WaitForGroupChatText(int fd, FrameBuffer& fb) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto frame = fb.TryPopFrame()) {
      if (HeaderFieldHost(frame->header, &WireHeader::type) != 0x030B) {
        continue;
      }
      if (frame->payload.size() < hiim::im::kHeaderSize) {
        continue;
      }
      const auto body = std::span<const uint8_t>(frame->payload.data() + hiim::im::kHeaderSize,
                                                 frame->payload.size() - hiim::im::kHeaderSize);
      return std::string(reinterpret_cast<const char*>(body.data()), body.size());
    }
    const auto remain_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - std::chrono::steady_clock::now())
                               .count();
    if (remain_ms <= 0) {
      break;
    }
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int pr = poll(&pfd, 1, static_cast<int>(std::min<int64_t>(remain_ms, 200)));
    if (pr <= 0) {
      continue;
    }
    uint8_t buf[4096];
    const ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
      fb.Append(std::span<const uint8_t>(buf, static_cast<std::size_t>(n)));
    }
  }
  return std::nullopt;
}

bool SendBackendDownlink(int backend_fd, uint32_t dest_nid, uint64_t seq,
                         std::string_view text) {
  const std::vector<uint8_t> body(text.begin(), text.end());
  const auto im_payload =
      hiim::im::PackPayload(0x030B, 100001, dest_nid, seq, body);
  const auto wire =
      EncodeFrame(0x030B, 31001, kFlagExp, im_payload);
  return WriteAll(backend_fd, wire);
}

// One TCP write with two downlink frames (msgsvr fan-out back-to-back on same conn).
bool SendBackendDualFanout(int backend_fd, uint32_t dest_a, uint32_t dest_b,
                           uint64_t seq, std::string_view text) {
  const std::vector<uint8_t> body(text.begin(), text.end());
  const auto im_a = hiim::im::PackPayload(0x030B, 100001, dest_a, seq, body);
  const auto im_b = hiim::im::PackPayload(0x030B, 100001, dest_b, seq, body);
  const auto wire_a = EncodeFrame(0x030B, 31001, kFlagExp, im_a);
  const auto wire_b = EncodeFrame(0x030B, 31001, kFlagExp, im_b);
  std::vector<uint8_t> batch;
  batch.reserve(wire_a.size() + wire_b.size());
  batch.insert(batch.end(), wire_a.begin(), wire_a.end());
  batch.insert(batch.end(), wire_b.begin(), wire_b.end());
  return WriteAll(backend_fd, batch);
}

int TestConcurrentDualFanout() {
  hiim::hub::DualHubConfig cfg;
  cfg.forward_listen = "127.0.0.1:39978";
  cfg.backend_listen = "127.0.0.1:39979";
  cfg.health_listen = "127.0.0.1:39079";
  cfg.reactor_threads = 2;
  cfg.worker_threads = 4;

  hiim::hub::HubServer hub(cfg);
  EXPECT_TRUE(hub.Start());
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  const int backend_fd = ConnectLocalhost(39979);
  EXPECT_TRUE(backend_fd >= 0);
  FrameBuffer backend_fb;
  EXPECT_TRUE(AuthAndSub(backend_fd, 31001, backend_fb));

  const int gw1_fd = ConnectLocalhost(39978);
  EXPECT_TRUE(gw1_fd >= 0);
  FrameBuffer gw1_fb;
  EXPECT_TRUE(AuthAndSub(gw1_fd, 20001, gw1_fb));

  const int gw2_fd = ConnectLocalhost(39978);
  EXPECT_TRUE(gw2_fd >= 0);
  FrameBuffer gw2_fb;
  EXPECT_TRUE(AuthAndSub(gw2_fd, 20002, gw2_fb));

  for (int round = 0; round < 10; ++round) {
    const uint64_t seq = static_cast<uint64_t>(106 + round);
    EXPECT_TRUE(SendBackendDualFanout(backend_fd, 20001, 20002, seq, "100001+4"));

    const auto gw1_msg = WaitForGroupChatText(gw1_fd, gw1_fb);
    EXPECT_TRUE(gw1_msg.has_value());
    EXPECT_TRUE(*gw1_msg == "100001+4");

    const auto gw2_msg = WaitForGroupChatText(gw2_fd, gw2_fb);
    EXPECT_TRUE(gw2_msg.has_value());
    EXPECT_TRUE(*gw2_msg == "100001+4");

    // Each gateway must receive exactly one frame per round (no duplicate / cross-route).
    EXPECT_TRUE(!DrainForMs(gw1_fd, gw1_fb, 100));
    EXPECT_TRUE(!DrainForMs(gw2_fd, gw2_fb, 100));
  }

  close(gw2_fd);
  close(gw1_fd);
  close(backend_fd);
  hub.Stop();
  hub.Wait();
  return 0;
}

int TestStickyDualFanoutRouting() {
  hiim::hub::DualHubConfig cfg;
  cfg.forward_listen = "127.0.0.1:39968";
  cfg.backend_listen = "127.0.0.1:39969";
  cfg.health_listen = "127.0.0.1:39078";
  cfg.reactor_threads = 2;
  cfg.worker_threads = 4;

  hiim::hub::HubServer hub(cfg);
  EXPECT_TRUE(hub.Start());
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  const int backend_fd = ConnectLocalhost(39969);
  EXPECT_TRUE(backend_fd >= 0);
  FrameBuffer backend_fb;
  EXPECT_TRUE(AuthAndSub(backend_fd, 31001, backend_fb));

  const int gw1_fd = ConnectLocalhost(39968);
  EXPECT_TRUE(gw1_fd >= 0);
  FrameBuffer gw1_fb;
  EXPECT_TRUE(AuthAndSub(gw1_fd, 20001, gw1_fb));

  const int gw2_fd = ConnectLocalhost(39968);
  EXPECT_TRUE(gw2_fd >= 0);
  FrameBuffer gw2_fb;
  EXPECT_TRUE(AuthAndSub(gw2_fd, 20002, gw2_fb));

  EXPECT_TRUE(SendBackendDualFanout(backend_fd, 20001, 20002, 107, "100001+5"));

  const auto gw1_msg = WaitForGroupChatText(gw1_fd, gw1_fb);
  EXPECT_TRUE(gw1_msg.has_value());
  EXPECT_TRUE(*gw1_msg == "100001+5");

  const auto gw2_msg = WaitForGroupChatText(gw2_fd, gw2_fb);
  EXPECT_TRUE(gw2_msg.has_value());
  EXPECT_TRUE(*gw2_msg == "100001+5");

  EXPECT_TRUE(!DrainForMs(gw1_fd, gw1_fb, 100));
  EXPECT_TRUE(!DrainForMs(gw2_fd, gw2_fb, 100));

  close(gw2_fd);
  close(gw1_fd);
  close(backend_fd);
  hub.Stop();
  hub.Wait();
  return 0;
}

int TestDualGatewayDownlinkRouting() {
  hiim::hub::DualHubConfig cfg;
  cfg.forward_listen = "127.0.0.1:39988";
  cfg.backend_listen = "127.0.0.1:39989";
  cfg.health_listen = "127.0.0.1:39080";
  cfg.reactor_threads = 1;
  cfg.worker_threads = 1;

  hiim::hub::HubServer hub(cfg);
  EXPECT_TRUE(hub.Start());
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  const int backend_fd = ConnectLocalhost(39989);
  EXPECT_TRUE(backend_fd >= 0);
  FrameBuffer backend_fb;
  EXPECT_TRUE(AuthAndSub(backend_fd, 31001, backend_fb));

  const int gw1_fd = ConnectLocalhost(39988);
  EXPECT_TRUE(gw1_fd >= 0);
  FrameBuffer gw1_fb;
  EXPECT_TRUE(AuthAndSub(gw1_fd, 20001, gw1_fb));

  const int gw2_fd = ConnectLocalhost(39988);
  EXPECT_TRUE(gw2_fd >= 0);
  FrameBuffer gw2_fb;
  EXPECT_TRUE(AuthAndSub(gw2_fd, 20002, gw2_fb));

  EXPECT_TRUE(SendBackendDownlink(backend_fd, 20001, 103, "100001+1"));
  const auto gw1_msg = WaitForGroupChatText(gw1_fd, gw1_fb);
  EXPECT_TRUE(gw1_msg.has_value());
  EXPECT_TRUE(*gw1_msg == "100001+1");
  EXPECT_TRUE(!DrainForMs(gw2_fd, gw2_fb, 300));

  EXPECT_TRUE(SendBackendDownlink(backend_fd, 20002, 106, "100001+4"));
  const auto gw2_msg = WaitForGroupChatText(gw2_fd, gw2_fb);
  EXPECT_TRUE(gw2_msg.has_value());
  EXPECT_TRUE(*gw2_msg == "100001+4");
  EXPECT_TRUE(!DrainForMs(gw1_fd, gw1_fb, 300));

  close(gw2_fd);
  close(gw1_fd);
  close(backend_fd);
  hub.Stop();
  hub.Wait();
  return 0;
}

int TestReadDestNidUsesOffset24() {
  const std::vector<uint8_t> body = {'x'};
  const auto payload = hiim::im::PackPayload(0x030B, 100001, 20002, 106, body);
  EXPECT_TRUE(hiim::im::ReadDestNid(payload) == 20002);
  // Length @ offset 4 is 1 — old bug read this as dest nid.
  uint32_t length_field = 0;
  std::memcpy(&length_field, payload.data() + hiim::im::kOffsetLength, sizeof(length_field));
  length_field = hiim::wire::BeToHost32(length_field);
  EXPECT_TRUE(length_field == 1);
  EXPECT_TRUE(length_field != hiim::im::ReadDestNid(payload));
  return 0;
}

}  // namespace

int main() {
  if (TestReadDestNidUsesOffset24() != 0) {
    return 1;
  }
  if (TestDualGatewayDownlinkRouting() != 0) {
    return 1;
  }
  if (TestStickyDualFanoutRouting() != 0) {
    return 1;
  }
  // Optional: HIIM_TEST_CONCURRENT=1 ./bridge_downlink_test
  if (std::getenv("HIIM_TEST_CONCURRENT") != nullptr) {
    if (TestConcurrentDualFanout() != 0) {
      return 1;
    }
  }
  std::cout << "bridge_downlink_test: OK\n";
  return 0;
}
