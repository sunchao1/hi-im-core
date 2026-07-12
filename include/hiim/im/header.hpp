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
// 文件: im/header.hpp
// 职责: 定义 IM 业务消息头（52 字节）布局及打包/解析工具
// 在系统中的位置: 业务层协议，嵌在 wire payload 内；bridge 下行路由读取 dest_nid/seq
// =============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "hiim/wire/header.hpp"

namespace hiim::im {

// 布局须与 hi-im-api/pkg/im/header 一致（Size=52，大端 MesgHeadHton）
static constexpr std::size_t kHeaderSize = 52;
static constexpr std::size_t kOffsetCmd = 0;      // 业务命令字偏移
static constexpr std::size_t kOffsetLength = 4;   // body 长度偏移
static constexpr std::size_t kOffsetSid = 8;      // 会话 ID 偏移
static constexpr std::size_t kOffsetCid = 16;     // 连接 ID 偏移（当前写零）
static constexpr std::size_t kOffsetNid = 24;     // 目标节点 ID 偏移（bridge 路由关键字段）
static constexpr std::size_t kOffsetSeq = 28;     // 消息序号偏移（日志/去重）

/// 主机序 → 大端序 64 位；写 IM 头 sid/seq 时使用
inline uint64_t HostToBe64(uint64_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(v);
#elif defined(_WIN32)
  return _byteswap_uint64(v);
#else
  return v;
#endif
}

/// 大端序 → 主机序 64 位
inline uint64_t BeToHost64(uint64_t v) { return HostToBe64(v); }

/// 将 IM 头写入缓冲区；cid 与 seq 后 16 字节填零，与 Go 侧 MesgHead 对齐
/// 调用方：PackPayload、worker 构造出站业务消息
inline void WriteHeader(std::span<uint8_t> buf, uint32_t cmd, uint32_t length, uint64_t sid,
                        uint32_t dest_nid, uint64_t seq) {
  if (buf.size() < kHeaderSize) {
    return;
  }
  const uint32_t cmd_be = hiim::wire::HostToBe32(cmd);
  const uint32_t len_be = hiim::wire::HostToBe32(length);
  const uint64_t sid_be = HostToBe64(sid);
  const uint32_t nid_be = hiim::wire::HostToBe32(dest_nid);
  const uint64_t seq_be = HostToBe64(seq);
  std::memcpy(buf.data() + kOffsetCmd, &cmd_be, sizeof(cmd_be));
  std::memcpy(buf.data() + kOffsetLength, &len_be, sizeof(len_be));
  std::memcpy(buf.data() + kOffsetSid, &sid_be, sizeof(sid_be));
  std::memset(buf.data() + kOffsetCid, 0, 8);
  std::memcpy(buf.data() + kOffsetNid, &nid_be, sizeof(nid_be));
  std::memcpy(buf.data() + kOffsetSeq, &seq_be, sizeof(seq_be));
  std::memset(buf.data() + kOffsetSeq + 8, 0, 16);
}

/// 打包 IM 头 + body 为连续字节；结果作为 wire 帧的 payload 发送
inline std::vector<uint8_t> PackPayload(uint32_t cmd, uint64_t sid, uint32_t dest_nid,
                                        uint64_t seq, std::span<const uint8_t> body) {
  std::vector<uint8_t> out(kHeaderSize + body.size());
  WriteHeader(out, cmd, static_cast<uint32_t>(body.size()), sid, dest_nid, seq);
  if (!body.empty()) {
    std::memcpy(out.data() + kHeaderSize, body.data(), body.size());
  }
  return out;
}

/// 从 payload 读取目标 nid（大端转主机序）；bridge 下行 AsyncSend 路由依据
/// 注意：payload 须含完整 IM 头，否则返回 0
inline uint32_t ReadDestNid(std::span<const uint8_t> payload) {
  if (payload.size() < kHeaderSize) {
    return 0;
  }
  uint32_t be_nid = 0;
  std::memcpy(&be_nid, payload.data() + kOffsetNid, sizeof(be_nid));
  return hiim::wire::BeToHost32(be_nid);
}

/// 从 payload 读取消息序号；bridge 日志与链路追踪使用
inline uint64_t ReadSeq(std::span<const uint8_t> payload) {
  if (payload.size() < kHeaderSize) {
    return 0;
  }
  uint64_t be_seq = 0;
  std::memcpy(&be_seq, payload.data() + kOffsetSeq, sizeof(be_seq));
  return BeToHost64(be_seq);
}

}  // namespace hiim::im
