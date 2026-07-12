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
// 文件：distributor.hpp
// 职责：从 DistQueue 消费 OutboundFrame，按 reactor_idx 路由到 SendQueue。
// 流水线角色：Listener → Reactor → Worker → Distributor 的下行分发层。
// 涉及队列：
//   - DistQueue（MPSC，Worker/Handler Push / 本 Distributor Pop）
//   - SendQueue[reactor_idx]（SPSC，本 Distributor Push / Reactor Pop）
// 执行线程：每个 HubContext 一个 Distributor 线程。
// =============================================================================

#pragma once

#include <atomic>
#include <thread>

#include "hiim/hub/context.hpp"

namespace hiim::hub {

// 下行分发器：将 OutboundFrame 从 DistQueue 路由到目标 Reactor 的 SendQueue。
class Distributor {
 public:
  explicit Distributor(HubContext& ctx);
  ~Distributor();

  Distributor(const Distributor&) = delete;
  Distributor& operator=(const Distributor&) = delete;

  void Start();
  void Stop();
  void Join();

 private:
  // 主循环：epoll_wait → Drain 唤醒 → Pop DistQueue → 路由到 SendQueue。
  void Run();

  HubContext& ctx_;
  std::thread thread_;
#if defined(__linux__)
  int epfd_{-1};
#else
  int kqfd_{-1};
#endif
};

}  // namespace hiim::hub
