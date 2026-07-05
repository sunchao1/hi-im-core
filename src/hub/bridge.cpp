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


#include "hiim/hub/bridge.hpp"

#include <iostream>

#include "hiim/im/header.hpp"
#include "hub/route_log.hpp"

namespace hiim::hub {

namespace {

uint32_t ReadImDestNid(const InboundMessage& msg) {
  return hiim::im::ReadDestNid(msg.payload);
}

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

void RegisterDefaultBridgeHandler(HubContext& ctx, MessageHandler handler) {
  ctx.RegisterHandler(0, std::move(handler));
}

}  // namespace

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
