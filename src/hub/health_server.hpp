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
// 文件：health_server.hpp
// 职责：HTTP 健康检查服务，提供 /healthz 和 /readyz 端点。
// 在系统中的位置：独立于 bus wire 流水线，供 K8s/Docker 探活使用。
// /healthz：进程存活即 200；/readyz：双平面均已监听才 200。
// =============================================================================

#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace hiim::hub {

class HubServer;

// 简易 HTTP 健康检查服务器；单线程 accept + 一次性响应后关闭连接。
class HealthServer {
 public:
  HealthServer(std::string listen_addr, HubServer* hub);
  ~HealthServer();

  // 绑定端口并启动 Run 线程。
  bool Start();
  // 关闭 listen socket，Run 循环退出。
  void Stop();
  // 等待 Run 线程结束。
  void Join();

 private:
  // accept 循环：读 HTTP 请求 → 判断路径 → 写响应 → close。
  void Run();
  // 从 listen_addr 解析端口号。
  bool ParsePort(int& port) const;

  std::string listen_addr_;
  HubServer* hub_{nullptr};
  int listen_fd_{-1};
  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace hiim::hub
