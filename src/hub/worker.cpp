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
// 文件：worker.cpp
// 职责：消费 RecvQueue 中的 InboundMessage，分派到 cmd 对应的 Handler。
// 流水线角色：Listener → Reactor → Worker → Distributor 的业务处理层。
// 涉及队列：RecvQueue[idx]（MPSC，Reactor Push / 本 Worker Pop）。
// 执行线程：Worker 专用线程。
// =============================================================================

#include "hub/worker.hpp"

#include <unistd.h>

#include "hiim/wire/header.hpp"

#if defined(__linux__)
#include <sys/epoll.h>
#else
#include <sys/event.h>
#include <sys/time.h>
#endif

namespace hiim::hub {

Worker::Worker(HubContext& ctx, int idx) : ctx_(ctx), idx_(idx) {}

Worker::~Worker() { Stop(); }

// --- 启动：创建 epoll 并注册 WorkerWakeup fd ---
void Worker::Start() {
#if defined(__linux__)
  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = ctx_.WorkerWakeup(idx_).Fd();
  epoll_ctl(epfd_, EPOLL_CTL_ADD, ev.data.fd, &ev);
#else
  kqfd_ = kqueue();
  struct kevent ev{};
  EV_SET(&ev, ctx_.WorkerWakeup(idx_).Fd(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
  kevent(kqfd_, &ev, 1, nullptr, 0, nullptr);
#endif
  thread_ = std::thread([this] { Run(); });
}

void Worker::Stop() { ctx_.RequestStop(); }

void Worker::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
#if defined(__linux__)
  if (epfd_ >= 0) {
    close(epfd_);
    epfd_ = -1;
  }
#else
  if (kqfd_ >= 0) {
    close(kqfd_);
    kqfd_ = -1;
  }
#endif
}

// --- 主循环：等待唤醒 → Pop RecvQueue → 调用 Handler ---
void Worker::Run() {
  auto& q = ctx_.RecvQueue(idx_);
  while (ctx_.Running().load(std::memory_order_acquire)) {
#if defined(__linux__)
    epoll_event events[4];
    const int n = epoll_wait(epfd_, events, 4, 0);
    for (int i = 0; i < n; ++i) {
      if (events[i].data.fd == ctx_.WorkerWakeup(idx_).Fd()) {
        ctx_.WorkerWakeup(idx_).Drain();
      }
    }
#else
    struct kevent events[4];
    const timespec timeout{0, 200 * 1000 * 1000};
    const int n = kevent(kqfd_, nullptr, 0, events, 4, &timeout);
    for (int i = 0; i < n; ++i) {
      if (static_cast<int>(events[i].ident) == ctx_.WorkerWakeup(idx_).Fd()) {
        ctx_.WorkerWakeup(idx_).Drain();
      }
    }
#endif

    // 批量 Pop 并分派；Handler 可能调用 Publish/AsyncSend 写入 DistQueue
    while (auto msg = q.Pop()) {
      MessageHandler handler = ctx_.FindHandler(msg->type);
      if (!handler) {
        handler = ctx_.FindHandler(0);
      }
      if (handler) {
        handler(ctx_, *msg);
      }
    }
  }
}

}  // namespace hiim::hub
