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

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "hiim/status.hpp"

namespace hiim::hub {

struct Subscriber {
  uint32_t gid{0};
  uint64_t sid{0};
  uint32_t nid{0};
  int reactor_idx{-1};
};

struct NidRoute {
  uint64_t sid{0};
  int reactor_idx{-1};
};

class Router {
 public:
  Status Subscribe(uint32_t cmd, const Subscriber& sub);
  Status Unsubscribe(uint32_t cmd, uint64_t sid);
  void RemoveSession(uint64_t sid, uint32_t nid);

  std::vector<Subscriber> FindSubscribers(uint32_t cmd) const;
  std::optional<NidRoute> FindNidRoute(uint32_t nid) const;

  void BindNid(uint32_t nid, uint64_t sid, int reactor_idx);
  void UnbindNid(uint32_t nid, uint64_t sid);

  std::size_t SubscriptionCount(uint32_t cmd) const;

 private:
  mutable std::shared_mutex mu_;
  std::unordered_map<uint32_t, std::vector<Subscriber>> sub_table_;
  std::unordered_map<uint32_t, NidRoute> nid_map_;
};

}  // namespace hiim::hub
