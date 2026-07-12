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

// =============================================================================
// 文件：pipe_wakeup.hpp
// 职责：跨线程唤醒机制，让 epoll/kqueue 线程感知队列有新数据。
// 流水线角色：Listener → Reactor → Worker → Distributor 各阶段的唤醒通道。
// 涉及唤醒点：
//   - ReactorWakeup：Listener/Distributor Notify → Reactor Drain
//   - WorkerWakeup：Reactor Notify → Worker Drain
//   - DistWakeup：Publish/AsyncSend Notify → Distributor Drain
// Linux 使用 eventfd，macOS 使用 pipe。
// =============================================================================

#pragma once

#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/eventfd.h>
#endif

#include <utility>

namespace hiim::hub {

// 跨线程唤醒：生产者 Notify，消费者注册 fd 到 epoll 并在可读时 Drain。
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

  // 供 epoll/kqueue 注册的可读 fd。
  int Fd() const { return fd_; }
  bool Valid() const { return fd_ >= 0; }

  // 生产者调用：写入计数/字节，触发 epoll 可读事件。
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

  // 消费者调用：读空 eventfd/pipe，避免 epoll 持续触发。
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
