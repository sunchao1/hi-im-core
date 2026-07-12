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
// 文件: wire/header.hpp
// 职责: 定义 TCP 传输帧头（20 字节定长）及编解码工具函数
// 在系统中的位置: 协议层核心，reactor 收包、distributor 发包、bridge 转发均依赖此格式
// =============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace hiim::wire {

/// 帧头魔数校验和，用于快速识别合法 wire 帧
static constexpr uint32_t kWireChecksum = 0x1FE23DC4u;
/// 系统命令帧标志（认证/心跳/订阅等）
static constexpr uint32_t kFlagSys = 0;
/// 业务扩展帧标志（IM 消息、Publish/AsyncSend 路由）
static constexpr uint32_t kFlagExp = 1;
/// 帧头固定长度（字节）
static constexpr std::size_t kWireHeaderSize = 20;

#pragma pack(push, 1)
/// wire 传输帧头；所有多字节字段均为大端序存储
struct WireHeader {
  uint32_t type;    // 命令字：SysCmd 或业务 cmd
  uint32_t nid;     // 节点 ID，路由与认证绑定用
  uint32_t flag;    // kFlagSys / kFlagExp，区分系统帧与业务帧
  uint32_t length;  // payload 字节长度（不含帧头）
  uint32_t chksum;  // 固定魔数 kWireChecksum
};
#pragma pack(pop)

static_assert(sizeof(WireHeader) == kWireHeaderSize, "WireHeader must be 20 bytes");

/// 主机序 → 大端序 32 位；小端主机在编码帧头时调用
inline uint32_t HostToBe32(uint32_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap32(v);
#elif defined(_WIN32)
  return _byteswap_ulong(v);
#else
  return v;
#endif
}

/// 大端序 → 主机序 32 位；解码帧头字段时调用
inline uint32_t BeToHost32(uint32_t v) { return HostToBe32(v); }

/// 校验帧头 chksum 字段；reactor 收包后首道合法性检查
inline bool ValidateChecksum(const WireHeader& h) {
  return BeToHost32(h.chksum) == kWireChecksum;
}

/// 构造帧头（字段自动转大端）；Publish/AsyncSend/EncodeFrame 内部使用
inline WireHeader MakeHeader(uint32_t type, uint32_t nid, uint32_t flag,
                             uint32_t payload_len) {
  WireHeader h{};
  h.type = HostToBe32(type);
  h.nid = HostToBe32(nid);
  h.flag = HostToBe32(flag);
  h.length = HostToBe32(payload_len);
  h.chksum = HostToBe32(kWireChecksum);
  return h;
}

/// 将帧头序列化到字节缓冲区；out 不足 kWireHeaderSize 时静默返回
inline void EncodeHeader(const WireHeader& src, std::span<uint8_t> out) {
  if (out.size() < kWireHeaderSize) {
    return;
  }
  std::memcpy(out.data(), &src, kWireHeaderSize);
}

/// 从字节流解析帧头并校验 chksum；失败返回 false（数据不足或校验不通过）
inline bool DecodeHeader(std::span<const uint8_t> in, WireHeader& out) {
  if (in.size() < kWireHeaderSize) {
    return false;
  }
  std::memcpy(&out, in.data(), kWireHeaderSize);
  if (!ValidateChecksum(out)) {
    return false;
  }
  return true;
}

/// 组装完整 wire 帧（帧头 + payload）；distributor 出队后直接或经 reactor 发送
inline std::vector<uint8_t> EncodeFrame(uint32_t type, uint32_t nid, uint32_t flag,
                                        std::span<const uint8_t> payload) {
  const auto hdr = MakeHeader(type, nid, flag,
                              static_cast<uint32_t>(payload.size()));
  std::vector<uint8_t> frame(kWireHeaderSize + payload.size());
  EncodeHeader(hdr, frame);
  if (!payload.empty()) {
    std::memcpy(frame.data() + kWireHeaderSize, payload.data(), payload.size());
  }
  return frame;
}

/// 读取帧头某字段并转主机序；避免各处重复 BeToHost32 样板代码
inline uint32_t HeaderFieldHost(const WireHeader& h, uint32_t WireHeader::* field) {
  return BeToHost32(h.*field);
}

}  // namespace hiim::wire
