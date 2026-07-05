#pragma once

#include <thread>

#include "hub/queue.hpp"

namespace hiim::hub {

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
