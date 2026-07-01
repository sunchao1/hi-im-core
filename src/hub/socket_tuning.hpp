#pragma once

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace hiim::hub {

inline void TuneTcpSocket(int fd) {
  if (fd < 0) {
    return;
  }
  int yes = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
  int buf = 512 * 1024;
  setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
}

}  // namespace hiim::hub
