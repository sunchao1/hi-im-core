#include "hiim/hub/router.hpp"

#include <algorithm>

namespace hiim::hub {

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

std::vector<Subscriber> Router::FindSubscribers(uint32_t cmd) const {
  std::shared_lock lock(mu_);
  const auto it = sub_table_.find(cmd);
  if (it == sub_table_.end()) {
    return {};
  }
  return it->second;
}

std::optional<NidRoute> Router::FindNidRoute(uint32_t nid) const {
  std::shared_lock lock(mu_);
  const auto it = nid_map_.find(nid);
  if (it == nid_map_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void Router::BindNid(uint32_t nid, uint64_t sid, int reactor_idx) {
  std::unique_lock lock(mu_);
  nid_map_[nid] = NidRoute{sid, reactor_idx};
}

void Router::UnbindNid(uint32_t nid, uint64_t sid) {
  std::unique_lock lock(mu_);
  const auto it = nid_map_.find(nid);
  if (it != nid_map_.end() && it->second.sid == sid) {
    nid_map_.erase(it);
  }
}

std::size_t Router::SubscriptionCount(uint32_t cmd) const {
  std::shared_lock lock(mu_);
  const auto it = sub_table_.find(cmd);
  if (it == sub_table_.end()) {
    return 0;
  }
  return it->second.size();
}

}  // namespace hiim::hub
