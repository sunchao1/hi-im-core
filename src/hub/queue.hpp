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

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace hiim::hub {

template <typename T>
class SpscQueue {
 public:
  explicit SpscQueue(std::size_t capacity)
      : capacity_(capacity + 1), slots_(capacity_) {}

  bool Push(T value) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t next = (tail + 1) % capacity_;
    if (next == head_.load(std::memory_order_acquire)) {
      return false;
    }
    slots_[tail] = std::move(value);
    tail_.store(next, std::memory_order_release);
    return true;
  }

  std::optional<T> Pop() {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    T value = std::move(slots_[head]);
    head_.store((head + 1) % capacity_, std::memory_order_release);
    return value;
  }

  std::size_t SizeApprox() const {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if (tail >= head) {
      return tail - head;
    }
    return capacity_ - head + tail;
  }

  std::size_t Capacity() const { return capacity_ - 1; }

 private:
  const std::size_t capacity_;
  std::vector<T> slots_;
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

// Multi-producer / single-consumer queue (workers Push, distributor Pop).
template <typename T>
class MpscQueue {
 public:
  explicit MpscQueue(std::size_t capacity) : capacity_(capacity) {}

  bool Push(T value) {
    std::lock_guard lock(mu_);
    if (queue_.size() >= capacity_) {
      return false;
    }
    queue_.push_back(std::move(value));
    return true;
  }

  std::optional<T> Pop() {
    std::lock_guard lock(mu_);
    if (queue_.empty()) {
      return std::nullopt;
    }
    T out = std::move(queue_.front());
    queue_.pop_front();
    return out;
  }

  std::size_t Capacity() const { return capacity_; }

 private:
  const std::size_t capacity_;
  std::mutex mu_;
  std::deque<T> queue_;
};

}  // namespace hiim::hub
