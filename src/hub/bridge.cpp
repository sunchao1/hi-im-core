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
// 文件：bridge.cpp
// 职责：实现 FORWARD ↔ BACKEND 双平面桥接逻辑（非独立线程，跑在 Worker 内）。
// 规则 1（上行）：FORWARD 收到 hubclient 业务帧 → peer.Publish(cmd, payload)
// 规则 2（下行）：BACKEND 收到 msgsvr 业务帧 → 读 IM dest_nid → peer.AsyncSend
// 调用方：HubServer 构造时 RegisterBridgeHandlers，注册为 cmd=0 默认 handler。
// =============================================================================

#include "hiim/hub/bridge.hpp"

#include <iostream>

#include "hiim/im/header.hpp"
#include "hub/route_log.hpp"

namespace hiim::hub {

namespace {

// 从 InboundMessage.payload 解析 IM 头中的目标 NID（offset 24）。
// bridge 下行路由的唯一依据；不解析 Protobuf。
uint32_t ReadImDestNid(const InboundMessage& msg) {
  return hiim::im::ReadDestNid(msg.payload);
}

// FORWARD 平面上行 handler：gateway 发来的业务帧转投 BACKEND Publish。
// 执行线程：FORWARD Worker。
// 路由键：msg.type（cmd），查 BACKEND SUB 表。
void ForwardUplinkHandler(HubContext& ctx, const InboundMessage& msg) {
  HubContext* peer = ctx.Peer();
  if (peer == nullptr) {
    return;
  }
  const Status st = peer->Publish(msg.type, msg.payload.data(), msg.payload.size());
  if (!st.ok() && st.code != StatusCode::kNotFound) {
    std::cerr << "[bridge] forward publish failed: " << st.message << "\n";
  }
}

// BACKEND 平面下行 handler：msgsvr fan-out 帧转投 FORWARD AsyncSend。
// 执行线程：BACKEND Worker。
// 路由键：payload 内 IM.dest_nid（gateway NID，如 20001/20002）。
void BackendDownlinkHandler(HubContext& ctx, const InboundMessage& msg) {
  HubContext* peer = ctx.Peer();
  if (peer == nullptr) {
    return;
  }
  const uint32_t dest_nid = ReadImDestNid(msg);
  const uint64_t im_seq = hiim::im::ReadSeq(msg.payload);
  std::cerr << "[bridge] backend recv downlink dest_nid=" << dest_nid << " seq=" << im_seq
            << " cmd=0x" << std::hex << msg.type << std::dec << " sid=" << msg.sid << "\n";
  if (dest_nid == 0) {
    std::cerr << "[bridge] missing IM dest nid seq=" << im_seq << "\n";
    return;
  }
  const Status st =
      peer->AsyncSend(msg.type, dest_nid, msg.payload.data(), msg.payload.size());
  LogBridgeAsyncSend(dest_nid, im_seq, msg.type, st);
}

// 将 handler 注册为 cmd=0 的默认处理器。
// Worker 在 FindHandler(type) 失败时会 fallback 到 cmd=0。
void RegisterDefaultBridgeHandler(HubContext& ctx, MessageHandler handler) {
  ctx.RegisterHandler(0, std::move(handler));
}

}  // namespace

// 对外入口：互设 peer 并注册双平面默认 bridge handler。
// HubServer 构造时调用一次。
void RegisterBridgeHandlers(HubContext& forward_ctx, HubContext& backend_ctx) {
  forward_ctx.SetPeer(&backend_ctx);
  backend_ctx.SetPeer(&forward_ctx);

  RegisterDefaultBridgeHandler(
      forward_ctx, [](HubContext& ctx, const InboundMessage& msg) {
        ForwardUplinkHandler(ctx, msg);
      });
  RegisterDefaultBridgeHandler(
      backend_ctx, [](HubContext& ctx, const InboundMessage& msg) {
        BackendDownlinkHandler(ctx, msg);
      });
}

}  // namespace hiim::hub
