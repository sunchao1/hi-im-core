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
// 文件: hub/router.hpp
// 职责: 维护 cmd→订阅者 广播表与 nid→会话 单播路由表，线程安全读写
// 在系统中的位置: HubContext 内部组件，Publish/AsyncSend 查表，worker 认证/订阅时写表
// =============================================================================

#pragma once

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "hiim/status.hpp"

namespace hiim::hub {

/// 订阅者记录：某 cmd 的广播目标，含会话与 reactor 分片信息
struct Subscriber {
  uint32_t gid{0};       // 组 ID
  uint64_t sid{0};       // 会话 ID（连接唯一标识）
  uint32_t nid{0};       // 节点 ID（写入 wire 帧头）
  int reactor_idx{-1};   // 所属 reactor 索引，distributor 投递 send_queue 用
};

/// nid 单播路由：dest_nid → 具体连接，供 AsyncSend 点对点发送
struct NidRoute {
  uint64_t sid{0};
  int reactor_idx{-1};
};

/// 路由表；shared_mutex 保护，读多写少（Publish 频繁读，认证/断连写）
class Router {
 public:
  /// 注册/更新订阅；同 sid 重复订阅同一 cmd 时覆盖旧记录
  /// 调用方：worker 处理 kSubReq
  Status Subscribe(uint32_t cmd, const Subscriber& sub);

  /// 取消某会话对某 cmd 的订阅；不存在返回 kNotFound
  /// 调用方：worker 处理 kUnsubReq
  Status Unsubscribe(uint32_t cmd, uint64_t sid);

  /// 会话断开时批量清理：移除该 sid 全部订阅 + 解绑 nid
  /// 调用方：reactor/worker 连接关闭路径，避免僵尸路由
  void RemoveSession(uint64_t sid, uint32_t nid);

  /// 查找某 cmd 的全部订阅者副本；Publish 遍历发送
  /// 注意：返回 vector 副本，调用方不应在高频热路径外持有过久
  std::vector<Subscriber> FindSubscribers(uint32_t cmd) const;

  /// 按 nid 查找单播路由；AsyncSend 首步查表
  std::optional<NidRoute> FindNidRoute(uint32_t nid) const;

  /// 认证成功后绑定 nid 到会话；同 nid 新连接覆盖旧绑定
  /// 调用方：worker 处理 kAuthReq 成功分支
  void BindNid(uint32_t nid, uint64_t sid, int reactor_idx);

  /// 解绑 nid；仅当 sid 匹配时才删除，防止误删新连接
  void UnbindNid(uint32_t nid, uint64_t sid);

  /// 查询某 cmd 订阅者数量；health/metrics 使用
  std::size_t SubscriptionCount(uint32_t cmd) const;

 private:
  mutable std::shared_mutex mu_;
  std::unordered_map<uint32_t, std::vector<Subscriber>> sub_table_;  // cmd → 订阅者列表
  std::unordered_map<uint32_t, NidRoute> nid_map_;                 // nid → 单播路由
};

}  // namespace hiim::hub
