#pragma once

#include <string>

namespace hiim {

enum class StatusCode {
  kOk = 0,
  kInvalidArgument,
  kNotFound,
  kAlreadyExists,
  kUnauthenticated,
  kPermissionDenied,
  kQueueFull,
  kIoError,
  kInternal,
};

struct Status {
  StatusCode code{StatusCode::kOk};
  std::string message;

  static Status Ok() { return {}; }

  static Status Error(StatusCode code, std::string msg = {}) {
    Status s;
    s.code = code;
    s.message = std::move(msg);
    return s;
  }

  bool ok() const { return code == StatusCode::kOk; }
  explicit operator bool() const { return ok(); }
};

}  // namespace hiim
