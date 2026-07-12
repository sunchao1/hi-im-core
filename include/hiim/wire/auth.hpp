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
// 文件: wire/auth.hpp
// 职责: 定义认证请求 payload 布局及编解码，封装为 SysCmd::kAuthReq 帧
// 在系统中的位置: 协议层，客户端连接后首包、reactor/worker 认证流程使用
// =============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "hiim/wire/sys_cmd.hpp"

#include "hiim/wire/header.hpp"

namespace hiim::wire {

static constexpr std::size_t kAuthUserLen = 32;   // 用户名定长字段
static constexpr std::size_t kAuthPassLen = 16;   // 密码定长字段
static constexpr std::size_t kAuthPayloadSize = 4 + kAuthUserLen + kAuthPassLen + 4;

#pragma pack(push, 1)
/// 认证请求体；紧跟在 WireHeader 之后，type=kAuthReq、flag=kFlagSys
struct AuthPayload {
  uint32_t gid;                  // 组 ID（大端）
  char user[kAuthUserLen];       // 用户名，不足补零，超长截断
  char passwd[kAuthPassLen];     // 密码，同上
  uint32_t nid;                  // 客户端声明的节点 ID（大端）
};
#pragma pack(pop)

static_assert(sizeof(AuthPayload) == kAuthPayloadSize);

/// 构造认证 payload（字段转大端、字符串安全拷贝）；客户端/SDK 发认证包时调用
inline AuthPayload MakeAuthPayload(uint32_t gid, std::string_view user,
                                   std::string_view passwd, uint32_t nid) {
  AuthPayload p{};
  p.gid = HostToBe32(gid);
  std::memset(p.user, 0, kAuthUserLen);
  std::memset(p.passwd, 0, kAuthPassLen);
  if (!user.empty()) {
    std::memcpy(p.user, user.data(), std::min(user.size(), kAuthUserLen));
  }
  if (!passwd.empty()) {
    std::memcpy(p.passwd, passwd.data(), std::min(passwd.size(), kAuthPassLen));
  }
  p.nid = HostToBe32(nid);
  return p;
}

/// 从 payload 字节流反序列化 AuthPayload；长度不足返回 false
inline bool DecodeAuthPayload(std::span<const uint8_t> payload, AuthPayload& out) {
  if (payload.size() < kAuthPayloadSize) {
    return false;
  }
  std::memcpy(&out, payload.data(), kAuthPayloadSize);
  return true;
}

/// 编码完整认证帧（帧头 + AuthPayload）；type=kAuthReq，flag=kFlagSys
inline std::vector<uint8_t> EncodeAuthFrame(uint32_t gid, std::string_view user,
                                             std::string_view passwd, uint32_t nid) {
  const AuthPayload p = MakeAuthPayload(gid, user, passwd, nid);
  return EncodeFrame(static_cast<uint32_t>(SysCmd::kAuthReq), nid, kFlagSys,
                     std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&p),
                                              kAuthPayloadSize));
}

}  // namespace hiim::wire
