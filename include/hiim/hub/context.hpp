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
// 文件: hub/context.hpp
// 职责: Hub 单平面（Forward/Backend）运行时上下文：队列、唤醒、路由、消息分发
// 在系统中的位置: 核心枢纽，Listener/Reactor/Worker/Distributor 均持有引用并操作其队列
// =============================================================================

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

/// Hub 平面类型：Forward 面向客户端代理，Backend 面向后端服务
enum class Plane : uint8_t { kForward = 0, kBackend = 1 };

/// 单平面 Hub 配置；HubServer 将 DualHubConfig 拆为两份 HubConfig
struct HubConfig {
  std::string listen_addr{"0.0.0.0:28888"};
  int reactor_threads{4};       // IO 线程数，决定 conn/send 队列分片数
  int worker_threads{4};         // 业务线程数，决定 recv 队列分片数
  std::size_t queue_capacity{8192};  // 各无锁队列容量，满时 Publish/AsyncSend 返回 kQueueFull
  std::string auth_user{"proxy"};
  std::string auth_pass{"proxy"};
};

/// 出站帧：distributor → reactor send_queue → TCP 写出
struct OutboundFrame {
  uint64_t sid{0};              // 目标会话
  int reactor_idx{-1};          // 目标 reactor 分片
  std::vector<uint8_t> bytes;   // 完整 wire 帧（含 20 字节头）
};

/// 入站消息：reactor → worker recv_queue → handler 处理
struct InboundMessage {
  uint64_t sid{0};
  int reactor_idx{-1};          // 来源 reactor，便于回写时定位 send_queue
  uint32_t gid{0};
  uint32_t nid{0};              // 对端节点 ID
  uint32_t type{0};             // wire 帧 type（cmd 或 SysCmd）
  uint32_t flag{0};             // kFlagSys / kFlagExp
  std::vector<uint8_t> payload; // 帧体（IM 消息时含 52 字节 IM 头）
};

/// 新连接事件：listener → reactor conn_queue
struct NewConnection {
  int fd{-1};       // 已 accept 的 socket fd
  uint64_t sid{0};  // 分配的会话 ID
};

class HubContext;

/// 消息处理回调：worker 从 recv_queue 取出 InboundMessage 后分发
using MessageHandler = std::function<void(HubContext&, const InboundMessage&)>;

/// Hub 运行时上下文；每个平面（Forward/Backend）各持一份实例
class HubContext {
 public:
  explicit HubContext(Plane plane, HubConfig cfg);
  ~HubContext();

  /// 当前平面类型
  Plane GetPlane() const { return plane_; }

  /// 只读配置引用
  const HubConfig& Config() const { return cfg_; }

  /// 路由表（订阅/nid 绑定）
  Router& GetRouter() { return router_; }
  const Router& GetRouter() const { return router_; }

  /// 设置对端平面上下文；bridge 双向转发时 Forward↔Backend 互设
  void SetPeer(HubContext* peer) { peer_ = peer; }

  /// 获取对端平面；bridge handler 通过 peer 调用 Publish/AsyncSend
  HubContext* Peer() const { return peer_; }

  /// 注册 cmd → handler 映射；cmd=0 为默认 handler（bridge 兜底）
  /// 写锁保护 handlers_，worker 热路径通过 FindHandler 读锁查找
  void RegisterHandler(uint32_t cmd, MessageHandler handler);

  /// 查找 handler；未注册返回 nullptr
  MessageHandler FindHandler(uint32_t cmd) const;

  /// 广播：查订阅表，将帧推入 dist_queue，由 distributor 扇出到各 reactor send_queue
  /// 调用方：bridge 上行、业务 handler；队列满返回 kQueueFull
  Status Publish(uint32_t cmd, const uint8_t* data, std::size_t len);

  /// 单播：按 dest_nid 查路由，推入 dist_queue
  /// 调用方：bridge 下行；nid 不在线返回 kNotFound
  Status AsyncSend(uint32_t cmd, uint32_t dest_nid, const uint8_t* data, std::size_t len);

  /// 新连接队列（SPSC：listener 写，对应 reactor 读）
  SpscQueue<NewConnection>& ConnQueue(int reactor_idx);

  /// 入站消息队列（MPSC：多 reactor 写，对应 worker 读）
  MpscQueue<InboundMessage>& RecvQueue(int worker_idx);

  /// 出站帧队列（SPSC：distributor 写，对应 reactor 读）
  SpscQueue<OutboundFrame>& SendQueue(int reactor_idx);

  /// 分发队列（MPSC：worker/handler 写，distributor 读）
  MpscQueue<OutboundFrame>& DistQueue();

  /// reactor 侧 pipe 唤醒；队列有新数据时 Notify，避免 epoll 空转
  PipeWakeup& ReactorWakeup(int reactor_idx);

  /// worker 侧 pipe 唤醒
  PipeWakeup& WorkerWakeup(int worker_idx);

  /// distributor 侧 pipe 唤醒
  PipeWakeup& DistWakeup();

  /// 运行标志；false 时各线程循环退出
  std::atomic<bool>& Running() { return running_; }

  /// 请求优雅停止；设置 running_=false
  void RequestStop();

  /// 监听就绪标志；health 探活检查 IsListening
  void MarkListening(bool v) { listening_.store(v, std::memory_order_release); }
  bool IsListening() const { return listening_.load(std::memory_order_acquire); }

  /// 分配单调递增会话 ID；listener accept 时调用
  uint64_t NextSid();

  // 自由函数 Publish/AsyncSend 需访问私有队列，声明为 friend
  friend Status Publish(HubContext& ctx, uint32_t cmd, const uint8_t* data, std::size_t len);
  friend Status AsyncSend(HubContext& ctx, uint32_t cmd, uint32_t dest_nid,
                          const uint8_t* data, std::size_t len);

 private:
  Plane plane_;
  HubConfig cfg_;
  HubContext* peer_{nullptr};  // 对端平面，bridge 跨平面转发

  Router router_;

  std::atomic<bool> running_{true};
  std::atomic<bool> listening_{false};
  std::atomic<uint64_t> next_sid_{1};

  // 按 reactor_idx 分片的 SPSC 队列
  std::vector<std::unique_ptr<SpscQueue<NewConnection>>> conn_queues_;
  std::vector<std::unique_ptr<SpscQueue<OutboundFrame>>> send_queues_;
  std::vector<std::unique_ptr<PipeWakeup>> reactor_wakeups_;

  // 按 worker_idx 分片的 MPSC 队列
  std::vector<std::unique_ptr<MpscQueue<InboundMessage>>> recv_queues_;
  std::vector<std::unique_ptr<PipeWakeup>> worker_wakeups_;

  // 全局分发队列（多生产者单消费者）
  std::unique_ptr<MpscQueue<OutboundFrame>> dist_queue_;
  std::unique_ptr<PipeWakeup> dist_wakeup_;

  std::unordered_map<uint32_t, MessageHandler> handlers_;
  mutable std::shared_mutex handler_mu_;
};

/// 广播实现（context_impl.cpp）：查订阅 → 编码帧 → 推 dist_queue → 唤醒 distributor
Status Publish(HubContext& ctx, uint32_t cmd, const uint8_t* data, std::size_t len);

/// 单播实现（context_impl.cpp）：查 nid 路由 → 编码帧 → 推 dist_queue → 唤醒 distributor
Status AsyncSend(HubContext& ctx, uint32_t cmd, uint32_t dest_nid,
                 const uint8_t* data, std::size_t len);

}  // namespace hiim::hub
