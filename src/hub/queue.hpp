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
// 文件：queue.hpp
// 职责：提供 SPSC 与 MPSC 无锁/有锁队列，作为流水线各阶段的跨线程通道。
// 流水线角色：Listener → Reactor → Worker → Distributor 的队列基础设施。
// 涉及队列：
//   - SpscQueue：ConnQueue、SendQueue（单生产者单消费者）
//   - MpscQueue：RecvQueue、DistQueue（多生产者单消费者）
// =============================================================================

#pragma once

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace hiim::hub {

// 单生产者单消费者环形队列（无锁）。
// 用于 ConnQueue（Listener→Reactor）和 SendQueue（Distributor→Reactor）。
template <typename T>
class SpscQueue {
 public:
  explicit SpscQueue(std::size_t capacity)
      : capacity_(capacity + 1), slots_(capacity_) {}

  // 生产者 Push；队列满时返回 false。
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

  // 消费者 Pop；队列空时返回 nullopt。
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

// 多生产者单消费者队列（mutex 保护）。
// 用于 RecvQueue（Reactor→Worker）和 DistQueue（Worker/Handler→Distributor）。
template <typename T>
class MpscQueue {
 public:
  explicit MpscQueue(std::size_t capacity) : capacity_(capacity) {}

  // 多个 Reactor/Worker 线程可并发 Push。
  bool Push(T value) {
    std::lock_guard lock(mu_);
    if (queue_.size() >= capacity_) {
      return false;
    }
    queue_.push_back(std::move(value));
    return true;
  }

  // 仅 Distributor/Worker 线程 Pop。
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
