// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>

namespace nomos::rt {

// Fixed-capacity inline vector — a bounded, heap-free sequence container.
//
// All storage is inline (std::array<T, Capacity>); the type never allocates and
// never grows. It is the RT-safe replacement for std::vector where a small,
// known upper bound applies — e.g. building an OSC message (address + type tags
// + args) into a byte buffer on the control thread and carrying it by value
// through a lock-free queue.
//
// When T is trivially copyable, small_vector<T, N> is itself trivially copyable,
// so it can be a value element of spsc_queue with no heap and no per-slot
// indirection.
//
// Mutating operations are bounded: push_back / append return false instead of
// growing or invoking UB when the capacity would be exceeded — the caller
// decides how to handle an over-long payload. Intended for trivially
// constructible/copyable T (byte buffers); larger element types work but every
// slot is default-constructed.
template <typename T, std::size_t Capacity> class small_vector {
    static_assert(Capacity >= 1, "Capacity must be at least 1");

  public:
    using value_type = T;

    small_vector() = default;

    small_vector(std::initializer_list<T> init) noexcept {
        for (const T& v : init) {
            if (size_ == Capacity)
                break;
            data_[size_++] = v;
        }
    }

    // Append one element. Returns false (no effect) if already full.
    bool push_back(const T& v) noexcept {
        if (size_ == Capacity)
            return false;
        data_[size_++] = v;
        return true;
    }

    // Append n elements from src. All-or-nothing: returns false and appends
    // nothing if they would not all fit.
    bool append(const T* src, std::size_t n) noexcept {
        if (n > Capacity - size_)
            return false;
        for (std::size_t i = 0; i < n; ++i)
            data_[size_ + i] = src[i];
        size_ += n;
        return true;
    }

    void clear() noexcept { size_ = 0; }

    T*       data() noexcept { return data_.data(); }
    const T* data() const noexcept { return data_.data(); }

    std::size_t                  size() const noexcept { return size_; }
    bool                         empty() const noexcept { return size_ == 0; }
    bool                         full() const noexcept { return size_ == Capacity; }
    static constexpr std::size_t capacity() noexcept { return Capacity; }

    T&       operator[](std::size_t i) noexcept { return data_[i]; }
    const T& operator[](std::size_t i) const noexcept { return data_[i]; }

    T*       begin() noexcept { return data_.data(); }
    T*       end() noexcept { return data_.data() + size_; }
    const T* begin() const noexcept { return data_.data(); }
    const T* end() const noexcept { return data_.data() + size_; }

  private:
    std::array<T, Capacity> data_{};
    std::size_t             size_{0};
};

} // namespace nomos::rt
