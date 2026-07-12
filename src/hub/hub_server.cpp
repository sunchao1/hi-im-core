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
// 文件：hub_server.cpp
// 职责：双平面 Hub 服务生命周期管理——组装并启动全部线程组件。
// 启动顺序：Listener → Distributor → Reactor×N → Worker×M → HealthServer。
// 每个平面（FORWARD/BACKEND）各有一套完整流水线。
// =============================================================================

#include "hiim/hub/hub_server.hpp"

#include "hiim/hub/bridge.hpp"
#include "hub/distributor.hpp"
#include "hub/health_server.hpp"
#include "hub/listener.hpp"
#include "hub/reactor.hpp"
#include "hub/worker.hpp"

namespace hiim::hub {

// 构造双平面上下文并注册 bridge handler。
HubServer::HubServer(DualHubConfig cfg) : cfg_(std::move(cfg)) {
  HubConfig forward_cfg{};
  forward_cfg.listen_addr = cfg_.forward_listen;
  forward_cfg.reactor_threads = cfg_.reactor_threads;
  forward_cfg.worker_threads = cfg_.worker_threads;
  forward_cfg.queue_capacity = cfg_.queue_capacity;
  forward_cfg.auth_user = cfg_.auth_user;
  forward_cfg.auth_pass = cfg_.auth_pass;

  HubConfig backend_cfg = forward_cfg;
  backend_cfg.listen_addr = cfg_.backend_listen;

  forward_ = std::make_unique<HubContext>(Plane::kForward, forward_cfg);
  backend_ = std::make_unique<HubContext>(Plane::kBackend, backend_cfg);
  RegisterBridgeHandlers(*forward_, *backend_);
}

HubServer::~HubServer() { Stop(); }

// 启动双平面全部组件。
bool HubServer::Start() {
  const int reactors = std::max(1, cfg_.reactor_threads);
  const int workers = std::max(1, cfg_.worker_threads);

  // 1. Listener：accept 线程，推 connq
  forward_listener_ = std::make_unique<Listener>(*forward_, reactors);
  backend_listener_ = std::make_unique<Listener>(*backend_, reactors);
  if (!forward_listener_->Start() || !backend_listener_->Start()) {
    return false;
  }

  // 2. Distributor：单线程消费 distq，投 sendq
  forward_dist_ = std::make_unique<Distributor>(*forward_);
  backend_dist_ = std::make_unique<Distributor>(*backend_);
  forward_dist_->Start();
  backend_dist_->Start();

  // 3. Reactor×N：epoll IO 线程
  for (int i = 0; i < reactors; ++i) {
    auto fr = std::make_unique<Reactor>(*forward_, i, workers);
    auto br = std::make_unique<Reactor>(*backend_, i, workers);
    fr->Start();
    br->Start();
    forward_reactors_.push_back(std::move(fr));
    backend_reactors_.push_back(std::move(br));
  }
  // 4. Worker×M：业务处理线程（含 bridge handler）
  for (int i = 0; i < workers; ++i) {
    auto fw = std::make_unique<Worker>(*forward_, i);
    auto bw = std::make_unique<Worker>(*backend_, i);
    fw->Start();
    bw->Start();
    forward_workers_.push_back(std::move(fw));
    backend_workers_.push_back(std::move(bw));
  }

  // 5. HTTP 健康检查（/healthz、/readyz）
  health_ = std::make_unique<HealthServer>(cfg_.health_listen, this);
  return health_->Start();
}

// 请求停止：置 running 标志并关闭 listen socket。
void HubServer::Stop() {
  if (forward_) forward_->RequestStop();
  if (backend_) backend_->RequestStop();
  if (health_) health_->Stop();
  if (forward_listener_) forward_listener_->Stop();
  if (backend_listener_) backend_listener_->Stop();
}

// 阻塞等待所有工作线程退出。
void HubServer::Wait() {
  if (forward_listener_) forward_listener_->Join();
  if (backend_listener_) backend_listener_->Join();
  for (auto& r : forward_reactors_) r->Join();
  for (auto& r : backend_reactors_) r->Join();
  for (auto& w : forward_workers_) w->Join();
  for (auto& w : backend_workers_) w->Join();
  if (forward_dist_) forward_dist_->Join();
  if (backend_dist_) backend_dist_->Join();
  if (health_) health_->Join();
}

// 双平面均已监听就绪。
bool HubServer::IsReady() const {
  return forward_ && backend_ && forward_->IsListening() && backend_->IsListening();
}

}  // namespace hiim::hub
