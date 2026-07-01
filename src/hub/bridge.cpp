#include "hiim/hub/bridge.hpp"

#include <cstring>
#include <iostream>

#include "hiim/wire/header.hpp"

namespace hiim::hub {

namespace {

// IM MesgHeader layout (hi-im-api): type @0, nid @4 (48 bytes total).
static constexpr std::size_t kImHeaderSize = 48;
static constexpr std::size_t kImHeaderNidOffset = 4;

uint32_t ReadImDestNid(const InboundMessage& msg) {
  if (msg.payload.size() < kImHeaderSize) {
    return 0;
  }
  uint32_t be_nid = 0;
  std::memcpy(&be_nid, msg.payload.data() + kImHeaderNidOffset, sizeof(be_nid));
  return hiim::wire::BeToHost32(be_nid);
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
  if (dest_nid == 0) {
    std::cerr << "[bridge] missing IM dest nid\n";
    return;
  }
  const Status st =
      peer->AsyncSend(msg.type, dest_nid, msg.payload.data(), msg.payload.size());
  if (!st.ok()) {
    std::cerr << "[bridge] backend async_send nid=" << dest_nid << " err="
              << st.message << "\n";
  }
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
