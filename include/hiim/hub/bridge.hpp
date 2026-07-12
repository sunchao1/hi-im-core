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
// 文件: hub/bridge.hpp
// 职责: 注册 Forward↔Backend 跨平面桥接 handler，实现上行广播与下行单播
// 在系统中的位置: HubServer 构造时调用，连接两个平面的消息流转
// =============================================================================

#pragma once

#include "hiim/hub/context.hpp"

namespace hiim::hub {

/// 注册双平面桥接 handler 并互设 peer
/// - Forward 默认 handler：上行消息 → peer->Publish（广播到 Backend 订阅者）
/// - Backend 默认 handler：下行消息 → 解析 IM dest_nid → peer->AsyncSend（单播到 Forward 客户端）
/// 调用方：HubServer 构造函数（hub_server.cpp），无需业务方手动调用
void RegisterBridgeHandlers(HubContext& forward_ctx, HubContext& backend_ctx);

}  // namespace hiim::hub
