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
// 文件：reactor.cpp
// 职责：epoll/kqueue 事件循环，会话管理，协议帧解析与上下行队列桥接。
// 流水线角色：Listener → Reactor → Worker → Distributor 的核心 I/O 层。
// 涉及队列：
//   - ConnQueue[idx]（SPSC，Listener Push / 本 Reactor Pop）
//   - SendQueue[idx]（SPSC，Distributor Push / 本 Reactor Pop）
//   - RecvQueue[worker]（MPSC，本 Reactor Push / Worker Pop）
// 执行线程：每个 Reactor 实例独占一个线程。
// =============================================================================

#include "hub/reactor.hpp"

#include <cerrno>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hub/socket_tuning.hpp"

#include <cstring>
#include <iostream>

#include "hiim/im/header.hpp"
#include "hiim/wire/auth.hpp"
#include "hiim/wire/header.hpp"
#include "hiim/wire/sys_cmd.hpp"
#include "hub/queue_push.hpp"

#if defined(__linux__)
#include <sys/epoll.h>
#else
#include <sys/event.h>
#include <sys/time.h>
#endif

namespace hiim::hub {

namespace {

using hiim::wire::DecodeAuthPayload;
using hiim::wire::EncodeFrame;
using hiim::wire::FrameView;
using hiim::wire::HeaderFieldHost;
using hiim::wire::HostToBe32;
using hiim::wire::kFlagExp;
using hiim::wire::kFlagSys;
using hiim::wire::SysCmd;
using hiim::wire::WireHeader;

bool CredentialsMatch(const HubConfig& cfg, const hiim::wire::AuthPayload& auth) {
  const std::string user(auth.user, strnlen(auth.user, hiim::wire::kAuthUserLen));
  const std::string pass(auth.passwd, strnlen(auth.passwd, hiim::wire::kAuthPassLen));
  return user == cfg.auth_user && pass == cfg.auth_pass;
}

}  // namespace

Reactor::Reactor(HubContext& ctx, int idx, int worker_count)
    : ctx_(ctx), idx_(idx), worker_count_(std::max(1, worker_count)) {}

Reactor::~Reactor() { Stop(); }

// --- 启动：创建 epoll 并注册唤醒 fd ---
void Reactor::Start() {
#if defined(__linux__)
  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epfd_ < 0) {
    std::cerr << "[reactor] epoll_create1 failed\n";
    return;
  }
  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = ctx_.ReactorWakeup(idx_).Fd();
  epoll_ctl(epfd_, EPOLL_CTL_ADD, ev.data.fd, &ev);
#else
  kqfd_ = kqueue();
  if (kqfd_ < 0) {
    std::cerr << "[reactor] kqueue failed\n";
    return;
  }
  struct kevent ev{};
  EV_SET(&ev, ctx_.ReactorWakeup(idx_).Fd(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
  kevent(kqfd_, &ev, 1, nullptr, 0, nullptr);
#endif
  thread_ = std::thread([this] { Run(); });
}

void Reactor::Stop() { ctx_.RequestStop(); }

void Reactor::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
#if defined(__linux__)
  if (epfd_ >= 0) {
    close(epfd_);
    epfd_ = -1;
  }
#else
  if (kqfd_ >= 0) {
    close(kqfd_);
    kqfd_ = -1;
  }
#endif
}

// sid % worker_count：stick 规则，同一会话的业务帧始终进入同一 Worker。
int Reactor::PickWorker(uint64_t sid) const {
  return static_cast<int>(sid % static_cast<uint64_t>(worker_count_));
}

// 更新 epoll 对 session fd 的 EPOLLIN/EPOLLOUT 监听。
void Reactor::UpdateInterest(Session& session, bool want_read, bool want_write) {
#if defined(__linux__)
  epoll_event ev{};
  ev.data.fd = session.fd;
  if (want_read) ev.events |= EPOLLIN;
  if (want_write) ev.events |= EPOLLOUT;
  epoll_ctl(epfd_, EPOLL_CTL_MOD, session.fd, &ev);
#else
  struct kevent changes[2];
  int n = 0;
  EV_SET(&changes[n++], session.fd, EVFILT_READ, want_read ? EV_ADD : EV_DELETE, 0, 0, nullptr);
  EV_SET(&changes[n++], session.fd, EVFILT_WRITE, want_write ? EV_ADD : EV_DELETE, 0, 0, nullptr);
  kevent(kqfd_, changes, n, nullptr, 0, nullptr);
#endif
}

// --- 新连接入厂：从 ConnQueue Pop 并注册 epoll ---
void Reactor::DrainNewConnections() {
  auto& q = ctx_.ConnQueue(idx_);
  while (auto conn = q.Pop()) {
    Session session{};
    session.sid = conn->sid;
    session.fd = conn->fd;
    fd_to_sid_[session.fd] = session.sid;
    sessions_.emplace(session.sid, std::move(session));

    Session& s = sessions_[conn->sid];
    TuneTcpSocket(s.fd);
#if defined(__linux__)
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = s.fd;
    epoll_ctl(epfd_, EPOLL_CTL_ADD, s.fd, &ev);
#else
    struct kevent ev{};
    EV_SET(&ev, s.fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
    kevent(kqfd_, &ev, 1, nullptr, 0, nullptr);
#endif
  }
}

// --- 下行发送：从 SendQueue Pop 并写入 outbuf ---
void Reactor::DrainSendQueue() {
  auto& q = ctx_.SendQueue(idx_);
  while (auto item = q.Pop()) {
    const auto it = sessions_.find(item->sid);
    if (it == sessions_.end()) {
      std::cerr << "[reactor] drop send frame: session not found sid=" << item->sid
                << " reactor=" << idx_ << " bytes=" << item->bytes.size() << "\n";
      continue;
    }
    SendBytes(it->second, item->bytes);
  }
}

// 将待发数据追加到 outbuf，并立即尝试刷出。
bool Reactor::SendBytes(Session& session, std::span<const uint8_t> data) {
  if (data.empty()) {
    return true;
  }
  session.outbuf.insert(session.outbuf.end(), data.begin(), data.end());
  UpdateInterest(session, true, true);
  HandleWritable(session.fd);
  return true;
}

// --- 可写事件：非阻塞 send 刷出 outbuf ---
void Reactor::HandleWritable(int fd) {
  const auto fit = fd_to_sid_.find(fd);
  if (fit == fd_to_sid_.end()) {
    return;
  }
  Session& session = sessions_[fit->second];
  while (!session.outbuf.empty()) {
    const ssize_t n = send(session.fd, session.outbuf.data(), session.outbuf.size(), 0);
    if (n > 0) {
      session.outbuf.erase(session.outbuf.begin(),
                           session.outbuf.begin() + static_cast<std::ptrdiff_t>(n));
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    CloseSession(session.sid);
    return;
  }
  UpdateInterest(session, true, !session.outbuf.empty());
}

// --- 会话关闭：从 epoll / Router 移除 ---
void Reactor::CloseSession(uint64_t sid) {
  const auto it = sessions_.find(sid);
  if (it == sessions_.end()) {
    return;
  }
  Session& session = it->second;
  if (session.authed) {
    ctx_.GetRouter().RemoveSession(session.sid, session.nid);
  }
  fd_to_sid_.erase(session.fd);
#if defined(__linux__)
  epoll_ctl(epfd_, EPOLL_CTL_DEL, session.fd, nullptr);
#else
  struct kevent ev[2];
  EV_SET(&ev[0], session.fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
  EV_SET(&ev[1], session.fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
  kevent(kqfd_, ev, 2, nullptr, 0, nullptr);
#endif
  close(session.fd);
  sessions_.erase(it);
}

// --- 系统帧处理（AUTH / SUB / UNSUB / KPALIVE）---
bool Reactor::HandleSystem(Session& session, const FrameView& frame) {
  const uint32_t type = HeaderFieldHost(frame.header, &WireHeader::type);
  switch (static_cast<SysCmd>(type)) {
    case SysCmd::kAuthReq: {
      hiim::wire::AuthPayload auth{};
      if (!DecodeAuthPayload(std::span<const uint8_t>(frame.payload), auth)) {
        std::cerr << "[reactor] invalid AUTH payload sid=" << session.sid << "\n";
        CloseSession(session.sid);
        return false;
      }
      if (!CredentialsMatch(ctx_.Config(), auth)) {
        std::cerr << "[reactor] AUTH failed sid=" << session.sid << "\n";
        CloseSession(session.sid);
        return false;
      }
      session.authed = true;
      session.gid = hiim::wire::BeToHost32(auth.gid);
      session.nid = hiim::wire::BeToHost32(auth.nid);
      // 绑定 nid → (sid, reactor_idx) 供后续 AsyncSend 路由
      ctx_.GetRouter().BindNid(session.nid, session.sid, idx_);
      const auto ack = EncodeFrame(static_cast<uint32_t>(SysCmd::kAuthAck), session.nid,
                                   kFlagSys, std::span<const uint8_t>{});
      SendBytes(session, ack);
      return true;
    }
    case SysCmd::kSubReq: {
      if (!session.authed || frame.payload.size() < sizeof(uint32_t)) {
        CloseSession(session.sid);
        return false;
      }
      uint32_t cmd_be = 0;
      std::memcpy(&cmd_be, frame.payload.data(), sizeof(cmd_be));
      const uint32_t cmd = hiim::wire::BeToHost32(cmd_be);
      Subscriber sub{session.gid, session.sid, session.nid, idx_};
      ctx_.GetRouter().Subscribe(cmd, sub);
      const auto ack = EncodeFrame(static_cast<uint32_t>(SysCmd::kSubAck), session.nid,
                                   kFlagSys, std::span<const uint8_t>{});
      SendBytes(session, ack);
      return true;
    }
    case SysCmd::kUnsubReq: {
      if (!session.authed || frame.payload.size() < sizeof(uint32_t)) {
        return false;
      }
      uint32_t cmd_be = 0;
      std::memcpy(&cmd_be, frame.payload.data(), sizeof(cmd_be));
      const uint32_t cmd = hiim::wire::BeToHost32(cmd_be);
      ctx_.GetRouter().Unsubscribe(cmd, session.sid);
      const auto ack = EncodeFrame(static_cast<uint32_t>(SysCmd::kUnsubAck), session.nid,
                                   kFlagSys, std::span<const uint8_t>{});
      SendBytes(session, ack);
      return true;
    }
    case SysCmd::kKpaliveReq: {
      const auto ack = EncodeFrame(static_cast<uint32_t>(SysCmd::kKpaliveAck), session.nid,
                                   kFlagSys, std::span<const uint8_t>{});
      SendBytes(session, ack);
      return true;
    }
    default:
      return true;
  }
}

// --- 上行入队：业务帧 Push 到 RecvQueue 并唤醒 Worker ---
void Reactor::EnqueueInbound(Session& session, FrameView frame) {
  if (!session.authed) {
    CloseSession(session.sid);
    return;
  }
  InboundMessage msg{};
  msg.sid = session.sid;
  msg.reactor_idx = idx_;
  msg.gid = session.gid;
  msg.nid = session.nid;
  msg.type = HeaderFieldHost(frame.header, &WireHeader::type);
  msg.flag = HeaderFieldHost(frame.header, &WireHeader::flag);

  const int worker_idx = PickWorker(session.sid);
  if (msg.flag == kFlagExp && frame.payload.size() >= hiim::im::kHeaderSize) {
    const uint32_t wire_nid = HeaderFieldHost(frame.header, &WireHeader::nid);
    const uint32_t im_dest = hiim::im::ReadDestNid(frame.payload);
    const uint64_t im_seq = hiim::im::ReadSeq(frame.payload);
    std::cerr << "[reactor] enqueue inbound sid=" << session.sid << " reactor=" << idx_
              << " worker=" << worker_idx << " wire_nid=" << wire_nid
              << " im_dest_nid=" << im_dest << " seq=" << im_seq << " cmd=0x" << std::hex
              << msg.type << std::dec << "\n";
  }

  msg.payload = std::move(frame.payload);

  auto& q = ctx_.RecvQueue(worker_idx);
  if (!PushWithBackoff(q, std::move(msg))) {
    std::cerr << "[reactor] recv queue full sid=" << session.sid << " reactor=" << idx_
              << " worker=" << worker_idx << " cmd=0x" << std::hex << msg.type << std::dec
              << "\n";
    return;
  }
  // 唤醒目标 Worker 消费 RecvQueue
  ctx_.WorkerWakeup(worker_idx).Notify();
}

// --- 可读事件：收包、拆帧、系统/业务分发 ---
void Reactor::HandleReadable(int fd) {
  // 唤醒 fd：Drain pipe/eventfd 后处理 ConnQueue 和 SendQueue
  if (fd == ctx_.ReactorWakeup(idx_).Fd()) {
    ctx_.ReactorWakeup(idx_).Drain();
    DrainNewConnections();
    DrainSendQueue();
    return;
  }

  const auto fit = fd_to_sid_.find(fd);
  if (fit == fd_to_sid_.end()) {
    return;
  }
  Session& session = sessions_[fit->second];

  uint8_t buf[65536];
  while (true) {
    const ssize_t n = recv(session.fd, buf, sizeof(buf), 0);
    if (n > 0) {
      session.fb.Append(std::span<const uint8_t>(buf, static_cast<std::size_t>(n)));
      while (auto frame = session.fb.TryPopFrame()) {
        const uint32_t flag = HeaderFieldHost(frame->header, &WireHeader::flag);
        if (flag == kFlagSys) {
          if (!HandleSystem(session, *frame)) {
            return;
          }
        } else {
          EnqueueInbound(session, std::move(*frame));
        }
      }
      continue;
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
      CloseSession(session.sid);
    }
    break;
  }
}

// --- epoll/kqueue 主循环 ---
void Reactor::Run() {
  while (ctx_.Running().load(std::memory_order_acquire)) {
#if defined(__linux__)
    epoll_event events[64];
    const int n = epoll_wait(epfd_, events, 64, 0);
    for (int i = 0; i < n; ++i) {
      const int fd = events[i].data.fd;
      if (events[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
        HandleReadable(fd);
      }
      if (events[i].events & EPOLLOUT) {
        HandleWritable(fd);
      }
    }
#else
    struct kevent events[64];
    const timespec timeout{0, 200 * 1000 * 1000};
    const int n = kevent(kqfd_, nullptr, 0, events, 64, &timeout);
    for (int i = 0; i < n; ++i) {
      const int fd = static_cast<int>(events[i].ident);
      if (events[i].filter == EVFILT_READ) {
        HandleReadable(fd);
      } else if (events[i].filter == EVFILT_WRITE) {
        HandleWritable(fd);
      }
    }
#endif
    // 即使没有 epoll 事件也主动 drain SendQueue，避免遗漏
    DrainSendQueue();
  }

  // 退出前关闭所有残留会话
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    const uint64_t sid = it->first;
    ++it;
    CloseSession(sid);
  }
}

}  // namespace hiim::hub
