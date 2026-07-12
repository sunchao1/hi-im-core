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
// 文件：worker.hpp
// 职责：从 RecvQueue 消费 InboundMessage，按 cmd 分派到注册的 Handler。
// 流水线角色：Listener → Reactor → Worker → Distributor 的业务处理层。
// 涉及队列：RecvQueue[idx]（MPSC，Reactor Push / 本 Worker Pop）。
// 执行线程：每个 Worker 实例独占一个线程。
// =============================================================================

#pragma once

#include <atomic>
#include <thread>

#include "hiim/hub/context.hpp"

namespace hiim::hub {

// 业务 Worker：epoll 监听唤醒 fd，Pop RecvQueue 并调用 MessageHandler。
class Worker {
 public:
  Worker(HubContext& ctx, int idx);
  ~Worker();

  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  void Start();
  void Stop();
  void Join();

 private:
  // 主循环：epoll_wait → Drain 唤醒 → Pop RecvQueue → 调用 Handler。
  void Run();

  HubContext& ctx_;
  int idx_;
  std::thread thread_;
#if defined(__linux__)
  int epfd_{-1};
#else
  int kqfd_{-1};
#endif
};

}  // namespace hiim::hub
