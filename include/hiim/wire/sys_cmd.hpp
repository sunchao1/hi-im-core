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
// 文件: wire/sys_cmd.hpp
// 职责: 定义 wire 层系统命令字（认证、心跳、订阅、配置查询等）
// 在系统中的位置: 协议层常量，被 wire/auth.hpp、reactor/worker 解析帧时使用
// =============================================================================

#pragma once

#include <cstdint>

namespace hiim::wire {

/// 系统级命令字；对应 WireHeader.type，flag 为 kFlagSys 时生效
/// 与业务 IM 命令（flag=kFlagExp）区分，由 reactor/worker 优先处理
enum SysCmd : uint32_t {
  kAuthReq = 0x0001,      // 客户端认证请求
  kAuthAck = 0x0002,      // 服务端认证应答
  kKpaliveReq = 0x0003,   // 心跳请求
  kKpaliveAck = 0x0004,   // 心跳应答
  kSubReq = 0x0005,       // 订阅某 cmd 的广播
  kSubAck = 0x0006,       // 订阅确认
  kUnsubReq = 0x0007,     // 取消订阅
  kUnsubAck = 0x0008,     // 取消订阅确认
  kQueryConfReq = 0x1001, // 查询配置请求
  kQueryConfAck = 0x1002, // 查询配置应答
};

}  // namespace hiim::wire
