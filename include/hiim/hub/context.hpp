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
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "hiim/hub/router.hpp"
#include "hiim/status.hpp"
#include "hub/pipe_wakeup.hpp"
#include "hub/queue.hpp"

namespace hiim::hub {

enum class Plane : uint8_t { kForward = 0, kBackend = 1 };

struct HubConfig {
  std::string listen_addr{"0.0.0.0:28888"};
  int reactor_threads{4};
  int worker_threads{4};
  std::size_t queue_capacity{8192};
  std::string auth_user{"proxy"};
  std::string auth_pass{"proxy"};
};

struct OutboundFrame {
  uint64_t sid{0};
  int reactor_idx{-1};
  std::vector<uint8_t> bytes;
};

struct InboundMessage {
  uint64_t sid{0};
  int reactor_idx{-1};
  uint32_t gid{0};
  uint32_t nid{0};
  uint32_t type{0};
  uint32_t flag{0};
  std::vector<uint8_t> payload;
};

struct NewConnection {
  int fd{-1};
  uint64_t sid{0};
};

class HubContext;

using MessageHandler = std::function<void(HubContext&, const InboundMessage&)>;

class HubContext {
 public:
  explicit HubContext(Plane plane, HubConfig cfg);
  ~HubContext();

  Plane GetPlane() const { return plane_; }
  const HubConfig& Config() const { return cfg_; }
  Router& GetRouter() { return router_; }
  const Router& GetRouter() const { return router_; }

  void SetPeer(HubContext* peer) { peer_ = peer; }
  HubContext* Peer() const { return peer_; }

  void RegisterHandler(uint32_t cmd, MessageHandler handler);
  MessageHandler FindHandler(uint32_t cmd) const;

  Status Publish(uint32_t cmd, const uint8_t* data, std::size_t len);
  Status AsyncSend(uint32_t cmd, uint32_t dest_nid, const uint8_t* data, std::size_t len);

  SpscQueue<NewConnection>& ConnQueue(int reactor_idx);
  MpscQueue<InboundMessage>& RecvQueue(int worker_idx);
  SpscQueue<OutboundFrame>& SendQueue(int reactor_idx);
  MpscQueue<OutboundFrame>& DistQueue();

  PipeWakeup& ReactorWakeup(int reactor_idx);
  PipeWakeup& WorkerWakeup(int worker_idx);
  PipeWakeup& DistWakeup();

  std::atomic<bool>& Running() { return running_; }
  void RequestStop();

  void MarkListening(bool v) { listening_.store(v, std::memory_order_release); }
  bool IsListening() const { return listening_.load(std::memory_order_acquire); }

  uint64_t NextSid();

  friend Status Publish(HubContext& ctx, uint32_t cmd, const uint8_t* data, std::size_t len);
  friend Status AsyncSend(HubContext& ctx, uint32_t cmd, uint32_t dest_nid,
                          const uint8_t* data, std::size_t len);

 private:
  Plane plane_;
  HubConfig cfg_;
  HubContext* peer_{nullptr};
  Router router_;
  std::atomic<bool> running_{true};
  std::atomic<bool> listening_{false};
  std::atomic<uint64_t> next_sid_{1};

  std::vector<std::unique_ptr<SpscQueue<NewConnection>>> conn_queues_;
  std::vector<std::unique_ptr<MpscQueue<InboundMessage>>> recv_queues_;
  std::vector<std::unique_ptr<SpscQueue<OutboundFrame>>> send_queues_;
  std::unique_ptr<MpscQueue<OutboundFrame>> dist_queue_;

  std::vector<std::unique_ptr<PipeWakeup>> reactor_wakeups_;
  std::vector<std::unique_ptr<PipeWakeup>> worker_wakeups_;
  std::unique_ptr<PipeWakeup> dist_wakeup_;

  std::unordered_map<uint32_t, MessageHandler> handlers_;
  mutable std::shared_mutex handler_mu_;
};

Status Publish(HubContext& ctx, uint32_t cmd, const uint8_t* data, std::size_t len);
Status AsyncSend(HubContext& ctx, uint32_t cmd, uint32_t dest_nid,
                 const uint8_t* data, std::size_t len);

}  // namespace hiim::hub
