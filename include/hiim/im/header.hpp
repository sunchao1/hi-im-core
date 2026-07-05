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

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "hiim/wire/header.hpp"

namespace hiim::im {

// Must match hi-im-api/pkg/im/header (Size=52, big-endian MesgHeadHton).
static constexpr std::size_t kHeaderSize = 52;
static constexpr std::size_t kOffsetCmd = 0;
static constexpr std::size_t kOffsetLength = 4;
static constexpr std::size_t kOffsetSid = 8;
static constexpr std::size_t kOffsetCid = 16;
static constexpr std::size_t kOffsetNid = 24;
static constexpr std::size_t kOffsetSeq = 28;

inline uint64_t HostToBe64(uint64_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(v);
#elif defined(_WIN32)
  return _byteswap_uint64(v);
#else
  return v;
#endif
}

inline uint64_t BeToHost64(uint64_t v) { return HostToBe64(v); }

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

inline std::vector<uint8_t> PackPayload(uint32_t cmd, uint64_t sid, uint32_t dest_nid,
                                        uint64_t seq, std::span<const uint8_t> body) {
  std::vector<uint8_t> out(kHeaderSize + body.size());
  WriteHeader(out, cmd, static_cast<uint32_t>(body.size()), sid, dest_nid, seq);
  if (!body.empty()) {
    std::memcpy(out.data() + kHeaderSize, body.data(), body.size());
  }
  return out;
}

inline uint32_t ReadDestNid(std::span<const uint8_t> payload) {
  if (payload.size() < kHeaderSize) {
    return 0;
  }
  uint32_t be_nid = 0;
  std::memcpy(&be_nid, payload.data() + kOffsetNid, sizeof(be_nid));
  return hiim::wire::BeToHost32(be_nid);
}

inline uint64_t ReadSeq(std::span<const uint8_t> payload) {
  if (payload.size() < kHeaderSize) {
    return 0;
  }
  uint64_t be_seq = 0;
  std::memcpy(&be_seq, payload.data() + kOffsetSeq, sizeof(be_seq));
  return BeToHost64(be_seq);
}

}  // namespace hiim::im
