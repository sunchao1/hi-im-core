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


#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "hiim/hub/context.hpp"

namespace hiim::hub {

struct DualHubConfig {
  std::string forward_listen{"0.0.0.0:28888"};
  std::string backend_listen{"0.0.0.0:28889"};
  int reactor_threads{4};
  int worker_threads{4};
  std::size_t queue_capacity{262144};
  std::string auth_user{"proxy"};
  std::string auth_pass{"proxy"};
  std::string health_listen{"0.0.0.0:8080"};
};

class HubServer {
 public:
  explicit HubServer(DualHubConfig cfg);
  ~HubServer();

  HubServer(const HubServer&) = delete;
  HubServer& operator=(const HubServer&) = delete;

  bool Start();
  void Stop();
  void Wait();

  HubContext& Forward() { return *forward_; }
  HubContext& Backend() { return *backend_; }

  bool IsReady() const;

 private:
  DualHubConfig cfg_;
  std::unique_ptr<HubContext> forward_;
  std::unique_ptr<HubContext> backend_;

  std::unique_ptr<class Listener> forward_listener_;
  std::unique_ptr<class Listener> backend_listener_;
  std::vector<std::unique_ptr<class Reactor>> forward_reactors_;
  std::vector<std::unique_ptr<class Reactor>> backend_reactors_;
  std::vector<std::unique_ptr<class Worker>> forward_workers_;
  std::vector<std::unique_ptr<class Worker>> backend_workers_;
  std::unique_ptr<class Distributor> forward_dist_;
  std::unique_ptr<class Distributor> backend_dist_;
  std::unique_ptr<class HealthServer> health_;
};

}  // namespace hiim::hub
