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
    const uint64_t one = 1;
    (void)write(fd_, &one, sizeof(one));
#else
    const char c = 1;
    (void)write(write_fd_, &c, 1);
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
