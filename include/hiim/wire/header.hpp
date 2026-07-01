#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace hiim::wire {

static constexpr uint32_t kWireChecksum = 0x1FE23DC4u;
static constexpr uint32_t kFlagSys = 0;
static constexpr uint32_t kFlagExp = 1;
static constexpr std::size_t kWireHeaderSize = 20;

#pragma pack(push, 1)
struct WireHeader {
  uint32_t type;
  uint32_t nid;
  uint32_t flag;
  uint32_t length;
  uint32_t chksum;
};
#pragma pack(pop)

static_assert(sizeof(WireHeader) == kWireHeaderSize, "WireHeader must be 20 bytes");

inline uint32_t HostToBe32(uint32_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap32(v);
#elif defined(_WIN32)
  return _byteswap_ulong(v);
#else
  return v;
#endif
}

inline uint32_t BeToHost32(uint32_t v) { return HostToBe32(v); }

inline bool ValidateChecksum(const WireHeader& h) {
  return BeToHost32(h.chksum) == kWireChecksum;
}

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

inline void EncodeHeader(const WireHeader& src, std::span<uint8_t> out) {
  if (out.size() < kWireHeaderSize) {
    return;
  }
  std::memcpy(out.data(), &src, kWireHeaderSize);
}

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

inline uint32_t HeaderFieldHost(const WireHeader& h, uint32_t WireHeader::* field) {
  return BeToHost32(h.*field);
}

}  // namespace hiim::wire
