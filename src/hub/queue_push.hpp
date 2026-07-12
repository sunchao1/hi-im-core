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
// 文件：queue_push.hpp
// 职责：队列 Push 的自旋退避辅助，在队列满时 yield 后重试。
// 流水线角色：Reactor / Distributor / HubContext 在 Push 队列时的通用工具。
// 涉及队列：RecvQueue、SendQueue、DistQueue 的 Push 路径。
// =============================================================================

#pragma once

#include <thread>

#include "hub/queue.hpp"

namespace hiim::hub {

// 带退避的 Push：队列满时自旋 + yield，避免立即丢弃。
// 调用线程：Reactor（Push RecvQueue）、Distributor（Push SendQueue）、
//           HubContext（Push DistQueue）。
template <typename Queue, typename T>
bool PushWithBackoff(Queue& queue, T value, int max_retries = 1000000) {
  for (int i = 0; i < max_retries; ++i) {
    if (queue.Push(std::move(value))) {
      return true;
    }
    if ((i & 0x3F) == 0) {
      std::this_thread::yield();
    }
  }
  return queue.Push(std::move(value));
}

}  // namespace hiim::hub
