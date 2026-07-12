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
// 文件：socket_tuning.hpp
// 职责：TCP socket 性能调优（NODELAY、收发缓冲区大小）。
// 流水线角色：Listener accept 后、Reactor 注册新连接时调用。
// =============================================================================

#pragma once

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace hiim::hub {

// 对新 accept 的 fd 设置 TCP_NODELAY 和 512KB 收发缓冲。
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
