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

#include <cstdint>

namespace hiim::wire {

enum SysCmd : uint32_t {
  kAuthReq = 0x0001,
  kAuthAck = 0x0002,
  kKpaliveReq = 0x0003,
  kKpaliveAck = 0x0004,
  kSubReq = 0x0005,
  kSubAck = 0x0006,
  kUnsubReq = 0x0007,
  kUnsubAck = 0x0008,
  kQueryConfReq = 0x1001,
  kQueryConfAck = 0x1002,
};

}  // namespace hiim::wire
