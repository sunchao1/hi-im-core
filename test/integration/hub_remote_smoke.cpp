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
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

std::string EnvOr(const char* key, const char* fallback) {
  const char* v = std::getenv(key);
  return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

int ConnectHostPort(const std::string& host, int port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  const std::string port_str = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
    return -1;
  }
  int fd = -1;
  for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
      break;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
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
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (auto frame = fb.TryPopFrame()) {
      return HeaderFieldHost(frame->header, &WireHeader::type) == expect_type;
    }
    uint8_t buf[4096];
    const ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
      fb.Append(std::span<const uint8_t>(buf, static_cast<std::size_t>(n)));
    } else if (n == 0) {
      return false;
    }
  }
  return false;
}

int RunSmoke() {
  const std::string forward_host = EnvOr("HIIM_FORWARD_HOST", "127.0.0.1");
  const int forward_port = std::stoi(EnvOr("HIIM_FORWARD_PORT", "28888"));
  const std::string backend_host = EnvOr("HIIM_BACKEND_HOST", "127.0.0.1");
  const int backend_port = std::stoi(EnvOr("HIIM_BACKEND_PORT", "28889"));
  const std::string auth_user = EnvOr("HIIM_AUTH_USER", "proxy");
  const std::string auth_pass = EnvOr("HIIM_AUTH_PASS", "proxy");

  // 本二进制供 docker compose accept 使用；ctest 阶段无 Hub 进程，勿注册为 CTest。
  const int backend_fd = ConnectHostPort(backend_host, backend_port);
  EXPECT_TRUE(backend_fd >= 0);

  const auto backend_auth = EncodeAuthFrame(1, auth_user, auth_pass, 30001);
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

  const int forward_fd = ConnectHostPort(forward_host, forward_port);
  EXPECT_TRUE(forward_fd >= 0);
  const auto forward_auth = EncodeAuthFrame(1, auth_user, auth_pass, 20001);
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
  return 0;
}

}  // namespace

int main() {
  if (RunSmoke() != 0) {
    return 1;
  }
  std::cout << "hub_remote_smoke: OK\n";
  return 0;
}
