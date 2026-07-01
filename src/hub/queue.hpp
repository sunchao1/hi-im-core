#pragma once

#include <atomic>
#include <cstddef>
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

}  // namespace hiim::hub
