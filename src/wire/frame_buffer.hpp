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

// frame_buffer.hpp — TCP 流式拼帧缓冲区
//
// 职责：
//   将 TCP 字节流还原为完整的 bus wire 帧（20 字节头 + payload）。
//   TCP 是面向字节流的协议，一次 recv 可能只收到半帧（半包），也可能一次
//   收到多帧（粘包），本模块负责跨多次读调用的累积与拆包。
//
// 在系统中的位置：
//   wire 层组件，由 Reactor 为每条 Proxy TCP 连接（Session）持有一个
//   FrameBuffer 实例：recv → Append → TryPopFrame → 系统帧本地处理 / 业务帧入队。

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "hiim/wire/header.hpp"

namespace hiim::wire {

// 已解析出的完整帧视图：固定长度 wire 头 + 独立持有的 payload 副本。
struct FrameView {
  WireHeader header;
  std::vector<uint8_t> payload;
};

// 单连接拼帧缓冲区，解决 TCP 半包/粘包问题。
//
// 典型用法（Reactor 读循环）：
//   1. recv 到临时缓冲区
//   2. Append 追加到内部 buf_
//   3. 循环 TryPopFrame，直到返回 nullopt（说明当前缓冲区内已无完整帧）
class FrameBuffer {
 public:
  // 将本次 recv 读到的字节追加到内部缓冲区，等待后续凑齐完整帧。
  void Append(std::span<const uint8_t> data) {
    buf_.insert(buf_.end(), data.begin(), data.end());
  }

  // 清空缓冲区（连接重置或协议错误恢复时使用）。
  void Clear() { buf_.clear(); }

  // 返回当前已缓冲、尚未拆出的字节数。
  std::size_t BufferedBytes() const { return buf_.size(); }

  // 尝试从缓冲区头部弹出一帧完整 wire 帧。
  //
  // 返回值：
  //   - 有完整帧：FrameView（头 + payload 副本）
  //   - 半包：std::nullopt，保留 buf_ 等待下次 Append 凑齐
  //   - 头校验失败：清空 buf_ 并返回 nullopt（防止脏数据无限累积）
  //
  // 粘包：一次 Append 后应循环调用本方法，直到返回 nullopt。
  std::optional<FrameView> TryPopFrame() {
    // 半包：连 20 字节 wire 头都凑不齐，继续等待
    if (buf_.size() < kWireHeaderSize) {
      return std::nullopt;
    }

    WireHeader hdr{};
    if (!DecodeHeader(std::span<const uint8_t>(buf_.data(), buf_.size()), hdr)) {
      // 头非法（如校验和错误），丢弃全部缓冲避免死循环
      buf_.clear();
      return std::nullopt;
    }

    const uint32_t payload_len = HeaderFieldHost(hdr, &WireHeader::length);
    const std::size_t frame_len = kWireHeaderSize + payload_len;
    // 半包：头已齐但 payload 未收全
    if (buf_.size() < frame_len) {
      return std::nullopt;
    }

    // 先拷贝 payload 再 erase：Reactor 可能在 EnqueueInbound 读取前
    // 因粘包连续 TryPopFrame 多次；若返回指向 buf_ 的 span，erase 后会悬垂。
    std::vector<uint8_t> payload(
        buf_.data() + kWireHeaderSize,
        buf_.data() + kWireHeaderSize + static_cast<std::ptrdiff_t>(payload_len));
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(frame_len));
    return FrameView{hdr, std::move(payload)};
  }

 private:
  std::vector<uint8_t> buf_;  // 跨多次 recv 累积的未拆包字节流
};

}  // namespace hiim::wire
