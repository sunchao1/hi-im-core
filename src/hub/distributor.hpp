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
#include <thread>

#include "hiim/hub/context.hpp"

namespace hiim::hub {

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
