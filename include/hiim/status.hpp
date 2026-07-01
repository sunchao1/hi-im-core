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
