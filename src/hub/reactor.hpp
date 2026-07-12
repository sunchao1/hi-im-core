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
// 文件：reactor.hpp
// 职责：epoll/kqueue I/O 多路复用，管理会话读写、协议解析与路由分发。
// 流水线角色：Listener → Reactor → Worker → Distributor 的核心 I/O 层。
// 涉及队列：
//   - ConnQueue[idx]（SPSC，Listener Push / 本 Reactor Pop）
//   - SendQueue[idx]（SPSC，Distributor Push / 本 Reactor Pop）
//   - RecvQueue[worker]（MPSC，本 Reactor Push / Worker Pop）
// 执行线程：每个 Reactor 实例独占一个线程。
// =============================================================================

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>

#include "hiim/hub/context.hpp"
#include "wire/frame_buffer.hpp"

namespace hiim::hub {

// I/O Reactor：epoll 驱动，负责连接生命周期、帧解析与上下行队列桥接。
class Reactor {
 public:
  Reactor(HubContext& ctx, int idx, int worker_count);
  ~Reactor();

  Reactor(const Reactor&) = delete;
  Reactor& operator=(const Reactor&) = delete;

  // 创建 epoll/kqueue 并启动 Run 线程。
  void Start();
  void Stop();
  void Join();

 private:
  // 单个 TCP 会话状态：fd、认证信息、收发缓冲。
  struct Session {
    uint64_t sid{0};
    int fd{-1};
    bool authed{false};
    uint32_t gid{0};
    uint32_t nid{0};
    hiim::wire::FrameBuffer fb;
    std::vector<uint8_t> outbuf;
  };

  // epoll/kqueue 主循环。
  void Run();
  // 从 ConnQueue 取出新连接并注册到 epoll。
  void DrainNewConnections();
  // 从 SendQueue 取出待发帧并写入 outbuf。
  void DrainSendQueue();
  // 处理 fd 可读事件：收包、拆帧、分发。
  void HandleReadable(int fd);
  // 处理 fd 可写事件：刷出 outbuf。
  void HandleWritable(int fd);
  // 关闭会话并从 epoll / Router 中移除。
  void CloseSession(uint64_t sid);
  // 将业务帧 Push 到 RecvQueue 并唤醒 Worker（sid stick 到固定 Worker）。
  void EnqueueInbound(Session& session, hiim::wire::FrameView frame);
  // 处理系统帧（AUTH/SUB/UNSUB/KPALIVE）。
  bool HandleSystem(Session& session, const hiim::wire::FrameView& frame);
  // 将数据追加到 outbuf 并触发写事件。
  bool SendBytes(Session& session, std::span<const uint8_t> data);
  // 更新 epoll 对 fd 的读/写监听。
  void UpdateInterest(Session& session, bool want_read, bool want_write);
  // sid % worker_count，保证同一会话始终路由到同一 Worker。
  int PickWorker(uint64_t sid) const;

  HubContext& ctx_;
  int idx_;
  int worker_count_;
  std::thread thread_;
  std::unordered_map<int, uint64_t> fd_to_sid_;
  std::unordered_map<uint64_t, Session> sessions_;
#if defined(__linux__)
  int epfd_{-1};
#else
  int kqfd_{-1};
#endif
};

}  // namespace hiim::hub
