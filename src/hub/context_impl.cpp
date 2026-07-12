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
// 文件：context_impl.cpp
// 职责：HubContext 构造/队列访问实现，以及 Publish / AsyncSend 核心路由逻辑。
// 关键点：Publish/AsyncSend 只查表 + 入 DistQueue，不写 TCP。
// 出站路径：DistQueue → Distributor → SendQueue → Reactor send()。
// =============================================================================

#include "hiim/hub/context.hpp"

#include <algorithm>
#include <mutex>

#include "hiim/wire/header.hpp"
#include "hub/queue_push.hpp"
#include "hub/route_log.hpp"

namespace hiim::hub {

// 构造单平面上下文：按 reactor/worker 数量创建四类队列与唤醒 fd。
HubContext::HubContext(Plane plane, HubConfig cfg)
    : plane_(plane), cfg_(std::move(cfg)) {
  const int reactors = std::max(1, cfg_.reactor_threads);
  const int workers = std::max(1, cfg_.worker_threads);

  // 每个 Reactor 一条 connq（SPSC）和 sendq（SPSC）
  for (int i = 0; i < reactors; ++i) {
    conn_queues_.push_back(
        std::make_unique<SpscQueue<NewConnection>>(cfg_.queue_capacity));
    send_queues_.push_back(
        std::make_unique<SpscQueue<OutboundFrame>>(cfg_.queue_capacity));
    reactor_wakeups_.push_back(std::make_unique<PipeWakeup>());
  }
  // 每个 Worker 一条 recvq（MPSC）
  for (int i = 0; i < workers; ++i) {
    recv_queues_.push_back(
        std::make_unique<MpscQueue<InboundMessage>>(cfg_.queue_capacity));
    worker_wakeups_.push_back(std::make_unique<PipeWakeup>());
  }
  // 全平面共享一条 distq（MPSC），由 Distributor 单线程消费
  dist_queue_ = std::make_unique<MpscQueue<OutboundFrame>>(cfg_.queue_capacity);
  dist_wakeup_ = std::make_unique<PipeWakeup>();
}

HubContext::~HubContext() = default;

// 注册 cmd → handler；cmd=0 为默认 handler（bridge 使用）。
void HubContext::RegisterHandler(uint32_t cmd, MessageHandler handler) {
  std::unique_lock lock(handler_mu_);
  handlers_[cmd] = std::move(handler);
}

// 查找 handler；未注册返回 nullptr，Worker 会 fallback 到 cmd=0。
MessageHandler HubContext::FindHandler(uint32_t cmd) const {
  std::shared_lock lock(handler_mu_);
  const auto it = handlers_.find(cmd);
  if (it == handlers_.end()) {
    return nullptr;
  }
  return it->second;
}

SpscQueue<NewConnection>& HubContext::ConnQueue(int reactor_idx) {
  return *conn_queues_.at(static_cast<std::size_t>(reactor_idx));
}

MpscQueue<InboundMessage>& HubContext::RecvQueue(int worker_idx) {
  return *recv_queues_.at(static_cast<std::size_t>(worker_idx));
}

SpscQueue<OutboundFrame>& HubContext::SendQueue(int reactor_idx) {
  return *send_queues_.at(static_cast<std::size_t>(reactor_idx));
}

MpscQueue<OutboundFrame>& HubContext::DistQueue() { return *dist_queue_; }

PipeWakeup& HubContext::ReactorWakeup(int reactor_idx) {
  return *reactor_wakeups_.at(static_cast<std::size_t>(reactor_idx));
}

PipeWakeup& HubContext::WorkerWakeup(int worker_idx) {
  return *worker_wakeups_.at(static_cast<std::size_t>(worker_idx));
}

PipeWakeup& HubContext::DistWakeup() { return *dist_wakeup_; }

void HubContext::RequestStop() { running_.store(false, std::memory_order_release); }

// 单调递增会话 ID；Listener accept 时为每个新 TCP 分配。
uint64_t HubContext::NextSid() {
  return next_sid_.fetch_add(1, std::memory_order_relaxed);
}

// 成员函数转调自由函数实现。
Status HubContext::Publish(uint32_t cmd, const uint8_t* data, std::size_t len) {
  return hiim::hub::Publish(*this, cmd, data, len);
}

Status HubContext::AsyncSend(uint32_t cmd, uint32_t dest_nid, const uint8_t* data,
                             std::size_t len) {
  return hiim::hub::AsyncSend(*this, cmd, dest_nid, data, len);
}

// 广播：查 SUB 表，为每个订阅者编码一帧并入 DistQueue。
// 路由键：cmd。publish 在实现上展开为多次「类 async_send」入队。
// 调用方：bridge 上行、业务 handler。
Status Publish(HubContext& ctx, uint32_t cmd, const uint8_t* data, std::size_t len) {
  const auto subs = ctx.GetRouter().FindSubscribers(cmd);
  if (subs.empty()) {
    return Status::Error(StatusCode::kNotFound, "no subscribers");
  }
  for (const auto& sub : subs) {
    const auto frame =
        hiim::wire::EncodeFrame(cmd, sub.nid, hiim::wire::kFlagExp,
                                std::span<const uint8_t>(data, len));
    const RouteLogMeta meta = ParseRouteLogMeta(frame);
    OutboundFrame out{};
    out.sid = sub.sid;               // 目标会话
    out.reactor_idx = sub.reactor_idx;  // 目标 Reactor（来自 SUB 记录）
    out.bytes = std::move(frame);
    if (!PushWithBackoff(ctx.DistQueue(), std::move(out))) {
      LogDistQueuePushFail(ctx.GetPlane(), meta, "publish");
      return Status::Error(StatusCode::kQueueFull);
    }
    LogPublishEnqueue(ctx.GetPlane(), sub.nid, sub.sid, sub.reactor_idx, meta);
  }
  ctx.DistWakeup().Notify();
  return Status::Ok();
}

// 单播：查 NID 表，编码一帧并入 DistQueue。
// 路由键：dest_nid。nid 不在线返回 kNotFound，不排队重试。
// 调用方：bridge 下行、msgsvr fan-out（经 BACKEND hubclient 进来）。
Status AsyncSend(HubContext& ctx, uint32_t cmd, uint32_t dest_nid,
                 const uint8_t* data, std::size_t len) {
  const auto route = ctx.GetRouter().FindNidRoute(dest_nid);
  if (!route.has_value()) {
    return Status::Error(StatusCode::kNotFound, "nid not connected");
  }

  const auto frame = hiim::wire::EncodeFrame(cmd, dest_nid, hiim::wire::kFlagExp,
                                             std::span<const uint8_t>(data, len));
  const RouteLogMeta meta = ParseRouteLogMeta(frame);
  OutboundFrame out{};
  out.sid = route->sid;
  out.reactor_idx = route->reactor_idx;
  out.bytes = std::move(frame);
  if (!PushWithBackoff(ctx.DistQueue(), std::move(out))) {
    LogDistQueuePushFail(ctx.GetPlane(), meta, "async_send");
    return Status::Error(StatusCode::kQueueFull);
  }
  LogAsyncSendEnqueue(ctx.GetPlane(), dest_nid, route->sid, route->reactor_idx, meta);
  ctx.DistWakeup().Notify();
  return Status::Ok();
}

}  // namespace hiim::hub
