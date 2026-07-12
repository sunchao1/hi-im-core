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
// 文件：route_log.hpp
// 职责：路由链路结构化日志，用于排查 bridge/distributor 丢包、错路由问题。
// 输出到 stderr，字段含 plane、dest_nid、seq、cmd、reactor_idx 等。
// 排查顺序：msgsvr 日志 → [bridge] → [hub] → [distributor] → [reactor]。
// =============================================================================

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

// 从完整 wire 帧字节中解析日志所需元数据。
struct RouteLogMeta {
  uint32_t wire_cmd{0};      // wire 头 type 字段
  uint32_t wire_nid{0};     // wire 头 nid 字段
  uint32_t im_dest_nid{0};  // payload 内 IM 头 dest_nid（offset 24）
  uint64_t im_seq{0};       // payload 内 IM 头 seq（offset 28）
};

// 解析 wire 帧 + IM 头，提取 RouteLogMeta；帧不完整时返回部分字段。
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

// 平面名称字符串，用于日志输出。
inline const char* PlaneName(Plane plane) {
  return plane == Plane::kForward ? "forward" : "backend";
}

// bridge 下行 AsyncSend 结果日志。
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

// AsyncSend 入 DistQueue 成功日志；检查 param dest_nid 与 IM 头 dest_nid 是否一致。
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

// Publish 入 DistQueue 成功日志。
inline void LogPublishEnqueue(Plane plane, uint32_t sub_nid, uint64_t route_sid,
                              int route_reactor, const RouteLogMeta& meta) {
  std::cerr << "[hub] publish enqueue plane=" << PlaneName(plane) << " sub_nid=" << sub_nid
            << " route_sid=" << route_sid << " route_reactor=" << route_reactor
            << " seq=" << meta.im_seq << " cmd=0x" << std::hex << meta.wire_cmd << std::dec
            << "\n";
}

// DistQueue Push 失败（队列满）日志。
inline void LogDistQueuePushFail(Plane plane, const RouteLogMeta& meta, const char* op) {
  std::cerr << "[hub] dist queue full plane=" << PlaneName(plane) << " op=" << op
            << " im_dest_nid=" << meta.im_dest_nid << " seq=" << meta.im_seq << " cmd=0x"
            << std::hex << meta.wire_cmd << std::dec << "\n";
}

// Distributor 路由到 SendQueue 成功日志。
inline void LogDistributorRouteOk(const OutboundFrame& frame, const RouteLogMeta& meta) {
  std::cerr << "[distributor] route ok sid=" << frame.sid << " reactor=" << frame.reactor_idx
            << " wire_nid=" << meta.wire_nid << " im_dest_nid=" << meta.im_dest_nid
            << " seq=" << meta.im_seq << " cmd=0x" << std::hex << meta.wire_cmd << std::dec
            << "\n";
}

// Distributor 路由失败日志（invalid reactor_idx / send queue full）。
inline void LogDistributorRouteFail(const OutboundFrame& frame, const RouteLogMeta& meta,
                                    const char* reason) {
  std::cerr << "[distributor] route fail sid=" << frame.sid << " reactor=" << frame.reactor_idx
            << " reason=" << reason << " wire_nid=" << meta.wire_nid
            << " im_dest_nid=" << meta.im_dest_nid << " seq=" << meta.im_seq << " cmd=0x"
            << std::hex << meta.wire_cmd << std::dec << "\n";
}

}  // namespace hiim::hub
