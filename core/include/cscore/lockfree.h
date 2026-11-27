// Wait-free hand-offs between the audio thread and everything else.
// Nothing here allocates or locks.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <algorithm>
#include <vector>

namespace cs {

// Latest value of a small trivially-copyable T, one writer, any readers.
// The writer never waits; a reader retries if it overlapped a write.
template <typename T>
class LatestValue {
  static_assert(std::is_trivially_copyable_v<T>);
  static constexpr std::size_t kWords = (sizeof(T) + 7) / 8;

public:
  void write(const T& value) noexcept {
    std::array<std::uint64_t, kWords> tmp{};
    std::memcpy(tmp.data(), static_cast<const void*>(&value), sizeof(T));
    const std::uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1);
    for (std::size_t i = 0; i < kWords; ++i)
      words_[i].store(tmp[i]);
    seq_.store(s + 2);
  }

  T read() const noexcept {
    std::array<std::uint64_t, kWords> tmp{};
    for (;;) {
      const std::uint32_t s1 = seq_.load();
      if (s1 & 1u)
        continue;
      for (std::size_t i = 0; i < kWords; ++i)
        tmp[i] = words_[i].load();
      if (seq_.load() == s1)
        break;
    }
    T value;
    std::memcpy(static_cast<void*>(&value), tmp.data(), sizeof(T));
    return value;
  }

private:
  std::atomic<std::uint32_t> seq_{0};
  std::array<std::atomic<std::uint64_t>, kWords> words_{};
};

// Three buffers: the producer fills `back` and publishes it, the consumer
// takes the newest published one as `front`. Neither side ever waits, and a
// buffer is never read while it is written.
template <typename T>
class TripleBuffer {
public:
  T& back() noexcept { return buffers_[back_]; }
  void publish() noexcept { back_ = middle_.exchange(back_ | kFresh) & kIndex; }
  bool hasFresh() const noexcept { return (middle_.load() & kFresh) != 0; }

  // Consumer: swap in the newest published buffer, if there is one.
  bool take() noexcept {
    if (!hasFresh())
      return false;
    front_ = middle_.exchange(front_) & kIndex;
    return true;
  }
  const T& front() const noexcept { return buffers_[front_]; }

private:
  static constexpr std::uint32_t kIndex = 3, kFresh = 4;
  std::array<T, 3> buffers_{};
  std::atomic<std::uint32_t> middle_{1};
  std::uint32_t back_ = 0;  // producer only
  std::uint32_t front_ = 2; // consumer only
};

// Single-producer, single-consumer ring. Capacity is fixed at construction;
// when full, new items are dropped (the display can miss samples, the audio
// thread can never block).
template <typename T>
class SpscRing {
public:
  explicit SpscRing(std::size_t capacityPow2 = 1u << 15) : data_(capacityPow2), mask_(capacityPow2 - 1) {}

  bool push(const T& item) noexcept {
    const std::size_t w = write_.load(std::memory_order_relaxed);
    if (w - read_.load(std::memory_order_acquire) >= data_.size())
      return false;
    data_[w & mask_] = item;
    write_.store(w + 1, std::memory_order_release);
    return true;
  }

  // Pops up to `max` items into `out`; returns how many.
  std::size_t pop(T* out, std::size_t max) noexcept {
    const std::size_t r = read_.load(std::memory_order_relaxed);
    const std::size_t n = std::min(max, write_.load(std::memory_order_acquire) - r);
    for (std::size_t i = 0; i < n; ++i)
      out[i] = data_[(r + i) & mask_];
    read_.store(r + n, std::memory_order_release);
    return n;
  }

private:
  std::vector<T> data_;
  std::size_t mask_;
  std::atomic<std::size_t> write_{0}, read_{0};
};

} // namespace cs
