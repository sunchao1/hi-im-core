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

// =============================================================================
// 文件：listener.cpp
// 职责：实现 TCP accept 循环，将 NewConnection 推入 ConnQueue 并唤醒 Reactor。
// 流水线角色：Listener → Reactor → Worker → Distributor 的入口。
// 涉及队列：ConnQueue[reactor_idx]（SPSC，本线程 Push / Reactor Pop）。
// 执行线程：Listener 专用线程。
// =============================================================================

#include "hub/listener.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <iostream>

#include "hub/socket_tuning.hpp"

namespace hiim::hub {

namespace {

int SetCloseOnExec(int fd) {
#if defined(FD_CLOEXEC)
  return fcntl(fd, F_SETFD, FD_CLOEXEC);
#else
  (void)fd;
  return 0;
#endif
}

int CreateTcpSocket() {
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd >= 0) {
    SetCloseOnExec(fd);
  }
  return fd;
}
int SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 轮询选择 Reactor 索引，将新连接均匀分配到各 Reactor。
int PickReactor(int reactor_count) {
  static std::atomic<uint64_t> round{0};
  const auto n = round.fetch_add(1, std::memory_order_relaxed);
  return static_cast<int>(n % static_cast<uint64_t>(reactor_count));
}

}  // namespace

Listener::Listener(HubContext& ctx, int reactor_count)
    : ctx_(ctx), reactor_count_(std::max(1, reactor_count)) {}

Listener::~Listener() { Stop(); }

bool Listener::ParseListenAddr(int& port) const {
  const auto& addr = ctx_.Config().listen_addr;
  const auto colon = addr.rfind(':');
  if (colon == std::string::npos) {
    return false;
  }
  port = std::stoi(addr.substr(colon + 1));
  return true;
}

// 创建 listen socket、bind、listen，并启动 Run 线程。
bool Listener::Start() {
  if (started_.exchange(true)) {
    return true;
  }

  int port = 0;
  if (!ParseListenAddr(port)) {
    std::cerr << "[listener] invalid listen addr: " << ctx_.Config().listen_addr << "\n";
    started_.store(false);
    return false;
  }

  listen_fd_ = CreateTcpSocket();
  if (listen_fd_ < 0) {
    std::cerr << "[listener] socket failed\n";
    started_.store(false);
    return false;
  }

  int yes = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(static_cast<uint16_t>(port));
  sa.sin_addr.s_addr = INADDR_ANY;
  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
    std::cerr << "[listener] bind failed on port " << port << "\n";
    close(listen_fd_);
    listen_fd_ = -1;
    started_.store(false);
    return false;
  }
  if (listen(listen_fd_, 512) < 0) {
    std::cerr << "[listener] listen failed\n";
    close(listen_fd_);
    listen_fd_ = -1;
    started_.store(false);
    return false;
  }

  ctx_.MarkListening(true);
  thread_ = std::thread([this] { Run(); });
  return true;
}

void Listener::Stop() {
  ctx_.RequestStop();
  if (listen_fd_ >= 0) {
    shutdown(listen_fd_, SHUT_RDWR);
    close(listen_fd_);
    listen_fd_ = -1;
  }
}

void Listener::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

// --- accept 主循环 ---
void Listener::Run() {
  while (ctx_.Running().load(std::memory_order_acquire)) {
    sockaddr_in client{};
    socklen_t len = sizeof(client);
    const int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client), &len);
    if (fd < 0) {
      if (!ctx_.Running().load(std::memory_order_acquire)) {
        break;
      }
      continue;
    }
    if (SetNonBlocking(fd) < 0) {
      close(fd);
      continue;
    }
    TuneTcpSocket(fd);

    const int reactor_idx = PickReactor(reactor_count_);
    NewConnection conn{};
    conn.fd = fd;
    conn.sid = ctx_.NextSid();

    // Push 到目标 Reactor 的 ConnQueue，失败则丢弃连接
    auto& q = ctx_.ConnQueue(reactor_idx);
    if (!q.Push(std::move(conn))) {
      std::cerr << "[listener] conn queue full, dropping fd\n";
      close(fd);
      continue;
    }
    // 唤醒 Reactor 线程处理新连接
    ctx_.ReactorWakeup(reactor_idx).Notify();
  }
}

}  // namespace hiim::hub
