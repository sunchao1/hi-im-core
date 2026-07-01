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
#include <string>
#include <thread>

namespace hiim::hub {

class HubServer;

class HealthServer {
 public:
  HealthServer(std::string listen_addr, HubServer* hub);
  ~HealthServer();

  bool Start();
  void Stop();
  void Join();

 private:
  void Run();
  bool ParsePort(int& port) const;

  std::string listen_addr_;
  HubServer* hub_{nullptr};
  int listen_fd_{-1};
  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace hiim::hub
