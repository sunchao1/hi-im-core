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
// 文件: hub/hub_server.hpp
// 职责: 双平面 Hub 服务入口，组装 Listener/Reactor/Worker/Distributor/Health 生命周期
// 在系统中的位置: 应用顶层，main 或测试代码创建 HubServer 并 Start/Wait/Stop
// =============================================================================

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "hiim/hub/context.hpp"

namespace hiim::hub {

/// 双平面（Forward + Backend）统一配置
struct DualHubConfig {
  std::string forward_listen{"0.0.0.0:28888"};  // 客户端代理接入端口
  std::string backend_listen{"0.0.0.0:28889"};  // 后端服务接入端口
  int reactor_threads{4};
  int worker_threads{4};
  std::size_t queue_capacity{262144};  // 生产环境默认更大队列
  std::string auth_user{"proxy"};
  std::string auth_pass{"proxy"};
  std::string health_listen{"0.0.0.0:8080"};  // HTTP 健康检查端口
};

/// Hub 服务主类；拥有两个 HubContext 及全部 IO/业务线程组件
class HubServer {
 public:
  explicit HubServer(DualHubConfig cfg);
  ~HubServer();

  HubServer(const HubServer&) = delete;
  HubServer& operator=(const HubServer&) = delete;

  /// 启动双平面监听、分发器、reactor/worker 线程及 health 服务
  /// 失败时（如端口占用）返回 false，调用方勿再 Wait
  bool Start();

  /// 请求停止：置 running 标志、关闭监听、唤醒各线程
  void Stop();

  /// 阻塞等待所有工作线程退出；通常在 main 信号处理后调用
  void Wait();

  /// Forward 平面上下文；外部注册 handler 或探活时使用
  HubContext& Forward() { return *forward_; }

  /// Backend 平面上下文
  HubContext& Backend() { return *backend_; }

  /// 双平面均已监听就绪；health 探活返回 200 的条件
  bool IsReady() const;

 private:
  DualHubConfig cfg_;
  std::unique_ptr<HubContext> forward_;
  std::unique_ptr<HubContext> backend_;

  // 前向声明避免头文件循环依赖，实现在 hub_server.cpp
  std::unique_ptr<class Listener> forward_listener_;
  std::unique_ptr<class Listener> backend_listener_;
  std::vector<std::unique_ptr<class Reactor>> forward_reactors_;
  std::vector<std::unique_ptr<class Reactor>> backend_reactors_;
  std::vector<std::unique_ptr<class Worker>> forward_workers_;
  std::vector<std::unique_ptr<class Worker>> backend_workers_;
  std::unique_ptr<class Distributor> forward_dist_;   // 消费 dist_queue，扇出到 send_queue
  std::unique_ptr<class Distributor> backend_dist_;
  std::unique_ptr<class HealthServer> health_;
};

}  // namespace hiim::hub
