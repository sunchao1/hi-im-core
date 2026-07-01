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

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "hiim/wire/sys_cmd.hpp"

#include "hiim/wire/header.hpp"

namespace hiim::wire {

static constexpr std::size_t kAuthUserLen = 32;
static constexpr std::size_t kAuthPassLen = 16;
static constexpr std::size_t kAuthPayloadSize = 4 + kAuthUserLen + kAuthPassLen + 4;

#pragma pack(push, 1)
struct AuthPayload {
  uint32_t gid;
  char user[kAuthUserLen];
  char passwd[kAuthPassLen];
  uint32_t nid;
};
#pragma pack(pop)

static_assert(sizeof(AuthPayload) == kAuthPayloadSize);

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

inline bool DecodeAuthPayload(std::span<const uint8_t> payload, AuthPayload& out) {
  if (payload.size() < kAuthPayloadSize) {
    return false;
  }
  std::memcpy(&out, payload.data(), kAuthPayloadSize);
  return true;
}

inline std::vector<uint8_t> EncodeAuthFrame(uint32_t gid, std::string_view user,
                                             std::string_view passwd, uint32_t nid) {
  const AuthPayload p = MakeAuthPayload(gid, user, passwd, nid);
  return EncodeFrame(static_cast<uint32_t>(SysCmd::kAuthReq), nid, kFlagSys,
                     std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&p),
                                              kAuthPayloadSize));
}

}  // namespace hiim::wire
