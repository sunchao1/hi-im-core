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


#include "hub/distributor.hpp"

#include <unistd.h>

#include "hiim/im/header.hpp"
#include "hiim/wire/header.hpp"
#include "hub/queue_push.hpp"

#include <iostream>

#if defined(__linux__)
#include <sys/epoll.h>
#else
#include <sys/event.h>
#include <sys/time.h>
#endif

namespace hiim::hub {

namespace {

void LogDroppedFrame(const OutboundFrame& frame, const char* reason) {
  uint32_t wire_cmd = 0;
  uint32_t im_dest_nid = 0;
  uint64_t im_seq = 0;
  if (frame.bytes.size() >= hiim::wire::kWireHeaderSize + hiim::im::kHeaderSize) {
    hiim::wire::WireHeader wh{};
    if (hiim::wire::DecodeHeader(
            std::span<const uint8_t>(frame.bytes.data(), frame.bytes.size()), wh)) {
      wire_cmd = hiim::wire::BeToHost32(wh.type);
    }
    const auto im = std::span<const uint8_t>(frame.bytes.data() + hiim::wire::kWireHeaderSize,
                                               frame.bytes.size() - hiim::wire::kWireHeaderSize);
    im_dest_nid = hiim::im::ReadDestNid(im);
    im_seq = hiim::im::ReadSeq(im);
  }
  std::cerr << "[distributor] drop frame sid=" << frame.sid
            << " reactor=" << frame.reactor_idx << " reason=" << reason
            << " wire_cmd=0x" << std::hex << wire_cmd << std::dec
            << " im_dest_nid=" << im_dest_nid << " seq=" << im_seq << "\n";
}

bool RouteToSendQueue(HubContext& ctx, OutboundFrame frame) {
  if (frame.reactor_idx < 0) {
    LogDroppedFrame(frame, "invalid reactor_idx");
    return false;
  }
  const OutboundFrame log_frame = frame;
  const int reactor_idx = frame.reactor_idx;
  auto& sendq = ctx.SendQueue(reactor_idx);
  if (!PushWithBackoff(sendq, std::move(frame))) {
    LogDroppedFrame(log_frame, "send queue full");
    return false;
  }
  ctx.ReactorWakeup(reactor_idx).Notify();
  return true;
}

}  // namespace

Distributor::Distributor(HubContext& ctx) : ctx_(ctx) {}

Distributor::~Distributor() { Stop(); }

void Distributor::Start() {
#if defined(__linux__)
  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = ctx_.DistWakeup().Fd();
  epoll_ctl(epfd_, EPOLL_CTL_ADD, ev.data.fd, &ev);
#else
  kqfd_ = kqueue();
  struct kevent ev{};
  EV_SET(&ev, ctx_.DistWakeup().Fd(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
  kevent(kqfd_, &ev, 1, nullptr, 0, nullptr);
#endif
  thread_ = std::thread([this] { Run(); });
}

void Distributor::Stop() { ctx_.RequestStop(); }

void Distributor::Join() {
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

void Distributor::Run() {
  auto route_frame = [this](OutboundFrame frame) {
    RouteToSendQueue(ctx_, std::move(frame));
  };

  while (ctx_.Running().load(std::memory_order_acquire)) {
#if defined(__linux__)
    epoll_event events[8];
    const int n = epoll_wait(epfd_, events, 8, 0);
    for (int i = 0; i < n; ++i) {
      if (events[i].data.fd == ctx_.DistWakeup().Fd()) {
        ctx_.DistWakeup().Drain();
      }
    }
#else
    struct kevent events[8];
    const timespec timeout{0, 200 * 1000 * 1000};
    const int n = kevent(kqfd_, nullptr, 0, events, 8, &timeout);
    for (int i = 0; i < n; ++i) {
      if (static_cast<int>(events[i].ident) == ctx_.DistWakeup().Fd()) {
        ctx_.DistWakeup().Drain();
      }
    }
#endif

    while (auto frame = ctx_.DistQueue().Pop()) {
      route_frame(std::move(*frame));
    }
  }
}

}  // namespace hiim::hub
