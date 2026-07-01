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
