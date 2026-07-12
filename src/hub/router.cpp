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
// 文件：router.cpp
// 职责：维护两张路由表——SUB 表（publish 用）和 NID 表（async_send 用）。
// SUB 表：cmd → [订阅者列表]；NID 表：nid → {sid, reactor_idx}。
// 写表时机：Reactor 处理 AUTH（BindNid）、SUB/UNSUB；断连时 RemoveSession。
// =============================================================================

#include "hiim/hub/router.hpp"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

namespace hiim::hub {

// 注册或更新订阅；同一 sid 重复 SUB 同一 cmd 时覆盖旧记录。
Status Router::Subscribe(uint32_t cmd, const Subscriber& sub) {
  std::unique_lock lock(mu_);
  auto& vec = sub_table_[cmd];
  const auto it = std::find_if(vec.begin(), vec.end(), [&](const Subscriber& s) {
    return s.sid == sub.sid;
  });
  if (it != vec.end()) {
    *it = sub;
    return Status::Ok();
  }
  vec.push_back(sub);
  return Status::Ok();
}

// 取消某会话对某 cmd 的订阅。
Status Router::Unsubscribe(uint32_t cmd, uint64_t sid) {
  std::unique_lock lock(mu_);
  const auto table_it = sub_table_.find(cmd);
  if (table_it == sub_table_.end()) {
    return Status::Error(StatusCode::kNotFound);
  }
  auto& vec = table_it->second;
  const auto it = std::remove_if(vec.begin(), vec.end(),
                                 [&](const Subscriber& s) { return s.sid == sid; });
  if (it == vec.end()) {
    return Status::Error(StatusCode::kNotFound);
  }
  vec.erase(it, vec.end());
  if (vec.empty()) {
    sub_table_.erase(table_it);
  }
  return Status::Ok();
}

// 连接断开时批量清理：移除该 sid 的全部 SUB 记录 + 解绑 nid。
void Router::RemoveSession(uint64_t sid, uint32_t nid) {
  std::unique_lock lock(mu_);
  for (auto& [cmd, vec] : sub_table_) {
    vec.erase(std::remove_if(vec.begin(), vec.end(),
                             [&](const Subscriber& s) { return s.sid == sid; }),
              vec.end());
  }
  const auto it = nid_map_.find(nid);
  if (it != nid_map_.end() && it->second.sid == sid) {
    nid_map_.erase(it);
  }
}

// 查找某 cmd 的全部订阅者；Publish 遍历此列表 fan-out 入 DistQueue。
std::vector<Subscriber> Router::FindSubscribers(uint32_t cmd) const {
  std::shared_lock lock(mu_);
  const auto it = sub_table_.find(cmd);
  if (it == sub_table_.end()) {
    return {};
  }
  return it->second;
}

// 按 nid 查找单播路由；AsyncSend 首步查表。
std::optional<NidRoute> Router::FindNidRoute(uint32_t nid) const {
  std::shared_lock lock(mu_);
  const auto it = nid_map_.find(nid);
  if (it == nid_map_.end()) {
    return std::nullopt;
  }
  return it->second;
}

// AUTH 成功后绑定 nid → (sid, reactor_idx)；新连接可覆盖同 nid 旧绑定。
void Router::BindNid(uint32_t nid, uint64_t sid, int reactor_idx) {
  std::unique_lock lock(mu_);
  nid_map_[nid] = NidRoute{sid, reactor_idx};
}

// 解绑 nid；仅 sid 匹配时才删除，防止误删新连接。
void Router::UnbindNid(uint32_t nid, uint64_t sid) {
  std::unique_lock lock(mu_);
  const auto it = nid_map_.find(nid);
  if (it != nid_map_.end() && it->second.sid == sid) {
    nid_map_.erase(it);
  }
}

// 查询某 cmd 订阅者数量；health/metrics 使用。
std::size_t Router::SubscriptionCount(uint32_t cmd) const {
  std::shared_lock lock(mu_);
  const auto it = sub_table_.find(cmd);
  if (it == sub_table_.end()) {
    return 0;
  }
  return it->second.size();
}

}  // namespace hiim::hub
