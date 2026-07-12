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
// 文件：listener.hpp
// 职责：TCP 监听与 accept，将新连接投递到 Reactor 的 ConnQueue。
// 流水线角色：Listener → Reactor → Worker → Distributor 的入口。
// 涉及队列：ConnQueue[reactor_idx]（SPSC，Listener Push / Reactor Pop）。
// 执行线程：独立 Listener 线程（每个 HubContext 一个）。
// =============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "hiim/hub/context.hpp"

namespace hiim::hub {

// TCP 监听器：accept 新连接后按轮询策略分配给某个 Reactor。
class Listener {
 public:
  Listener(HubContext& ctx, int reactor_count);
  ~Listener();

  Listener(const Listener&) = delete;
  Listener& operator=(const Listener&) = delete;

  // 绑定端口并启动监听线程；由 HubServer 主线程调用。
  bool Start();
  // 关闭 listen fd 并请求全局停止；可从任意线程调用。
  void Stop();
  // 等待监听线程退出；由 HubServer::Wait 调用。
  void Join();

 private:
  // 监听主循环：accept → Push ConnQueue → Notify ReactorWakeup。
  void Run();
  // 从配置解析监听端口。
  bool ParseListenAddr(int& port) const;

  HubContext& ctx_;
  int reactor_count_{1};
  int listen_fd_{-1};
  std::thread thread_;
  std::atomic<bool> started_{false};
};

}  // namespace hiim::hub
