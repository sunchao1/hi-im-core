#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "hiim/wire/header.hpp"

namespace hiim::wire {

struct FrameView {
  WireHeader header;
  std::span<const uint8_t> payload;
};

class FrameBuffer {
 public:
  void Append(std::span<const uint8_t> data) {
    buf_.insert(buf_.end(), data.begin(), data.end());
  }

  void Clear() { buf_.clear(); }

  std::size_t BufferedBytes() const { return buf_.size(); }

  std::optional<FrameView> TryPopFrame() {
    if (buf_.size() < kWireHeaderSize) {
      return std::nullopt;
    }

    WireHeader hdr{};
    if (!DecodeHeader(std::span<const uint8_t>(buf_.data(), buf_.size()), hdr)) {
      buf_.clear();
      return std::nullopt;
    }

    const uint32_t payload_len = HeaderFieldHost(hdr, &WireHeader::length);
    const std::size_t frame_len = kWireHeaderSize + payload_len;
    if (buf_.size() < frame_len) {
      return std::nullopt;
    }

    FrameView view{hdr, std::span<const uint8_t>(buf_.data() + kWireHeaderSize, payload_len)};
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(frame_len));
    return view;
  }

 private:
  std::vector<uint8_t> buf_;
};

}  // namespace hiim::wire
