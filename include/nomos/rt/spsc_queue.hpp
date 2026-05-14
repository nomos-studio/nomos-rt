// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace nomos::rt {

// Lock-free single-producer / single-consumer ring buffer.
//
// Capacity must be a power of two.  The queue can hold at most Capacity - 1
// elements simultaneously (one slot is always reserved to distinguish full
// from empty without a separate counter).
//
// push() is safe to call from exactly one thread.
// pop()  is safe to call from exactly one thread (different from the producer).
// empty() / size() are approximate — they are safe to call from either thread
// but may be stale by the time the caller acts on them.

template <typename T, std::size_t Capacity> class spsc_queue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

  public:
    // Returns true if the item was enqueued; false if the queue was full.
    bool push(T item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire))
            return false; // full
        slots_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Returns the front item if the queue is non-empty; std::nullopt otherwise.
    std::optional<T> pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return std::nullopt; // empty
        T item = std::move(slots_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return item;
    }

    bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    std::size_t size() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & mask_;
    }

  private:
    static constexpr std::size_t mask_ = Capacity - 1;

    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::array<T, Capacity> slots_{};
};

} // namespace nomos::rt
