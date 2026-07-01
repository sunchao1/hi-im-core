#pragma once

#include "hiim/hub/context.hpp"

namespace hiim::hub {

void RegisterBridgeHandlers(HubContext& forward_ctx, HubContext& backend_ctx);

}  // namespace hiim::hub
