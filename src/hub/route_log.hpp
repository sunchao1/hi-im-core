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

#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#include "hiim/hub/context.hpp"
#include "hiim/im/header.hpp"
#include "hiim/status.hpp"
#include "hiim/wire/header.hpp"

namespace hiim::hub {

struct RouteLogMeta {
  uint32_t wire_cmd{0};
  uint32_t wire_nid{0};
  uint32_t im_dest_nid{0};
  uint64_t im_seq{0};
};

inline RouteLogMeta ParseRouteLogMeta(std::span<const uint8_t> wire_bytes) {
  RouteLogMeta meta{};
  if (wire_bytes.size() < hiim::wire::kWireHeaderSize) {
    return meta;
  }
  hiim::wire::WireHeader wh{};
  if (!hiim::wire::DecodeHeader(wire_bytes, wh)) {
    return meta;
  }
  meta.wire_cmd = hiim::wire::BeToHost32(wh.type);
  meta.wire_nid = hiim::wire::BeToHost32(wh.nid);
  if (wire_bytes.size() >= hiim::wire::kWireHeaderSize + hiim::im::kHeaderSize) {
    const auto im = wire_bytes.subspan(hiim::wire::kWireHeaderSize);
    meta.im_dest_nid = hiim::im::ReadDestNid(im);
    meta.im_seq = hiim::im::ReadSeq(im);
  }
  return meta;
}

inline const char* PlaneName(Plane plane) {
  return plane == Plane::kForward ? "forward" : "backend";
}

inline void LogBridgeAsyncSend(uint32_t im_dest_nid, uint64_t im_seq, uint32_t cmd,
                               const Status& st) {
  if (st.ok()) {
    std::cerr << "[bridge] backend async_send ok dest_nid=" << im_dest_nid << " seq=" << im_seq
              << " cmd=0x" << std::hex << cmd << std::dec << "\n";
    return;
  }
  std::cerr << "[bridge] backend async_send fail dest_nid=" << im_dest_nid << " seq=" << im_seq
            << " cmd=0x" << std::hex << cmd << std::dec << " err=" << st.message << "\n";
}

inline void LogAsyncSendEnqueue(Plane plane, uint32_t dest_nid, uint64_t route_sid,
                                int route_reactor, const RouteLogMeta& meta) {
  std::cerr << "[hub] async_send enqueue plane=" << PlaneName(plane) << " dest_nid=" << dest_nid
            << " route_sid=" << route_sid << " route_reactor=" << route_reactor
            << " wire_nid=" << meta.wire_nid << " im_dest_nid=" << meta.im_dest_nid
            << " seq=" << meta.im_seq << " cmd=0x" << std::hex << meta.wire_cmd << std::dec
            << "\n";
  if (meta.im_dest_nid != 0 && meta.im_dest_nid != dest_nid) {
    std::cerr << "[hub] WARN async_send dest_nid mismatch param=" << dest_nid
              << " im_header=" << meta.im_dest_nid << " seq=" << meta.im_seq << "\n";
  }
}

inline void LogPublishEnqueue(Plane plane, uint32_t sub_nid, uint64_t route_sid,
                              int route_reactor, const RouteLogMeta& meta) {
  std::cerr << "[hub] publish enqueue plane=" << PlaneName(plane) << " sub_nid=" << sub_nid
            << " route_sid=" << route_sid << " route_reactor=" << route_reactor
            << " seq=" << meta.im_seq << " cmd=0x" << std::hex << meta.wire_cmd << std::dec
            << "\n";
}

inline void LogDistQueuePushFail(Plane plane, const RouteLogMeta& meta, const char* op) {
  std::cerr << "[hub] dist queue full plane=" << PlaneName(plane) << " op=" << op
            << " im_dest_nid=" << meta.im_dest_nid << " seq=" << meta.im_seq << " cmd=0x"
            << std::hex << meta.wire_cmd << std::dec << "\n";
}

inline void LogDistributorRouteOk(const OutboundFrame& frame, const RouteLogMeta& meta) {
  std::cerr << "[distributor] route ok sid=" << frame.sid << " reactor=" << frame.reactor_idx
            << " wire_nid=" << meta.wire_nid << " im_dest_nid=" << meta.im_dest_nid
            << " seq=" << meta.im_seq << " cmd=0x" << std::hex << meta.wire_cmd << std::dec
            << "\n";
}

inline void LogDistributorRouteFail(const OutboundFrame& frame, const RouteLogMeta& meta,
                                    const char* reason) {
  std::cerr << "[distributor] route fail sid=" << frame.sid << " reactor=" << frame.reactor_idx
            << " reason=" << reason << " wire_nid=" << meta.wire_nid
            << " im_dest_nid=" << meta.im_dest_nid << " seq=" << meta.im_seq << " cmd=0x"
            << std::hex << meta.wire_cmd << std::dec << "\n";
}

}  // namespace hiim::hub
