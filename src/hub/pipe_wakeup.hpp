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

#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/eventfd.h>
#endif

#include <utility>

namespace hiim::hub {

class PipeWakeup {
 public:
  PipeWakeup() {
#if defined(__linux__)
    fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#else
    int fds[2];
    if (pipe(fds) == 0) {
      read_fd_ = fds[0];
      write_fd_ = fds[1];
      fcntl(read_fd_, F_SETFL, O_NONBLOCK);
      fcntl(write_fd_, F_SETFL, O_NONBLOCK);
      fd_ = read_fd_;
    }
#endif
  }

  ~PipeWakeup() {
#if defined(__linux__)
    if (fd_ >= 0) {
      close(fd_);
    }
#else
    if (read_fd_ >= 0) close(read_fd_);
    if (write_fd_ >= 0) close(write_fd_);
#endif
  }

  PipeWakeup(const PipeWakeup&) = delete;
  PipeWakeup& operator=(const PipeWakeup&) = delete;

  int Fd() const { return fd_; }
  bool Valid() const { return fd_ >= 0; }

  void Notify() {
#if defined(__linux__)
    if (fd_ < 0) {
      return;
    }
    const uint64_t one = 1;
    const ssize_t n = write(fd_, &one, sizeof(one));
    (void)n;
#else
    if (write_fd_ < 0) {
      return;
    }
    const char c = 1;
    const ssize_t n = write(write_fd_, &c, 1);
    (void)n;
#endif
  }

  void Drain() {
#if defined(__linux__)
    uint64_t v = 0;
    while (read(fd_, &v, sizeof(v)) == static_cast<ssize_t>(sizeof(v))) {
    }
#else
    char buf[64];
    while (read(read_fd_, buf, sizeof(buf)) > 0) {
    }
#endif
  }

 private:
  int fd_{-1};
#if !defined(__linux__)
  int read_fd_{-1};
  int write_fd_{-1};
#endif
};

}  // namespace hiim::hub
