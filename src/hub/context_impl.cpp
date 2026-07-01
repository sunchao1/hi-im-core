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


#include "hiim/hub/context.hpp"

#include <algorithm>
#include <mutex>

#include "hiim/wire/header.hpp"
#include "hub/queue_push.hpp"

namespace hiim::hub {

HubContext::HubContext(Plane plane, HubConfig cfg)
    : plane_(plane), cfg_(std::move(cfg)) {
  const int reactors = std::max(1, cfg_.reactor_threads);
  const int workers = std::max(1, cfg_.worker_threads);

  for (int i = 0; i < reactors; ++i) {
    conn_queues_.push_back(
        std::make_unique<SpscQueue<NewConnection>>(cfg_.queue_capacity));
    send_queues_.push_back(
        std::make_unique<SpscQueue<OutboundFrame>>(cfg_.queue_capacity));
    reactor_wakeups_.push_back(std::make_unique<PipeWakeup>());
  }
  for (int i = 0; i < workers; ++i) {
    recv_queues_.push_back(
        std::make_unique<SpscQueue<InboundMessage>>(cfg_.queue_capacity));
    worker_wakeups_.push_back(std::make_unique<PipeWakeup>());
  }
  dist_queue_ = std::make_unique<SpscQueue<OutboundFrame>>(cfg_.queue_capacity);
  dist_wakeup_ = std::make_unique<PipeWakeup>();
}

HubContext::~HubContext() = default;

void HubContext::RegisterHandler(uint32_t cmd, MessageHandler handler) {
  std::unique_lock lock(handler_mu_);
  handlers_[cmd] = std::move(handler);
}

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

SpscQueue<InboundMessage>& HubContext::RecvQueue(int worker_idx) {
  return *recv_queues_.at(static_cast<std::size_t>(worker_idx));
}

SpscQueue<OutboundFrame>& HubContext::SendQueue(int reactor_idx) {
  return *send_queues_.at(static_cast<std::size_t>(reactor_idx));
}

SpscQueue<OutboundFrame>& HubContext::DistQueue() { return *dist_queue_; }

PipeWakeup& HubContext::ReactorWakeup(int reactor_idx) {
  return *reactor_wakeups_.at(static_cast<std::size_t>(reactor_idx));
}

PipeWakeup& HubContext::WorkerWakeup(int worker_idx) {
  return *worker_wakeups_.at(static_cast<std::size_t>(worker_idx));
}

PipeWakeup& HubContext::DistWakeup() { return *dist_wakeup_; }

void HubContext::RequestStop() { running_.store(false, std::memory_order_release); }

uint64_t HubContext::NextSid() {
  return next_sid_.fetch_add(1, std::memory_order_relaxed);
}

Status HubContext::Publish(uint32_t cmd, const uint8_t* data, std::size_t len) {
  return hiim::hub::Publish(*this, cmd, data, len);
}

Status HubContext::AsyncSend(uint32_t cmd, uint32_t dest_nid, const uint8_t* data,
                             std::size_t len) {
  return hiim::hub::AsyncSend(*this, cmd, dest_nid, data, len);
}

Status Publish(HubContext& ctx, uint32_t cmd, const uint8_t* data, std::size_t len) {
  const auto subs = ctx.GetRouter().FindSubscribers(cmd);
  if (subs.empty()) {
    return Status::Error(StatusCode::kNotFound, "no subscribers");
  }
  for (const auto& sub : subs) {
    const auto frame =
        hiim::wire::EncodeFrame(cmd, sub.nid, hiim::wire::kFlagExp,
                                std::span<const uint8_t>(data, len));
    OutboundFrame out{};
    out.sid = sub.sid;
    out.reactor_idx = sub.reactor_idx;
    out.bytes = std::move(frame);
    if (!PushWithBackoff(ctx.DistQueue(), std::move(out))) {
      return Status::Error(StatusCode::kQueueFull);
    }
  }
  ctx.DistWakeup().Notify();
  return Status::Ok();
}

Status AsyncSend(HubContext& ctx, uint32_t cmd, uint32_t dest_nid,
                 const uint8_t* data, std::size_t len) {
  const auto route = ctx.GetRouter().FindNidRoute(dest_nid);
  if (!route.has_value()) {
    return Status::Error(StatusCode::kNotFound, "nid not connected");
  }

  const auto frame = hiim::wire::EncodeFrame(cmd, dest_nid, hiim::wire::kFlagExp,
                                             std::span<const uint8_t>(data, len));
  OutboundFrame out{};
  out.sid = route->sid;
  out.reactor_idx = route->reactor_idx;
  out.bytes = std::move(frame);
  if (!PushWithBackoff(ctx.DistQueue(), std::move(out))) {
    return Status::Error(StatusCode::kQueueFull);
  }
  ctx.DistWakeup().Notify();
  return Status::Ok();
}

}  // namespace hiim::hub
