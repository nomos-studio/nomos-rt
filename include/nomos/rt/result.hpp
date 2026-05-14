// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Minimal result type for C++20.  Upgrades to std::expected on C++23.
//
// Usage:
//   nomos::rt::result<T, E>  — a value or an error
//   nomos::rt::unexpected<E> — construct an error result
//
// In C++23 these are std::expected / std::unexpected with no API change.

#include <utility>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L

#include <expected>
namespace nomos::rt {
template <typename T, typename E> using result = std::expected<T, E>;
template <typename E> using unexpected         = std::unexpected<E>;
} // namespace nomos::rt

#else

#include <variant>

namespace nomos::rt {

template <typename E> struct unexpected {
    E error;
    explicit unexpected(E e) : error(std::move(e)) {}
};

template <typename T, typename E> class result {
  public:
    result(T val) : data_(std::move(val)) {}               // NOLINT(*-explicit*)
    result(unexpected<E> u) : data_(std::move(u.error)) {} // NOLINT(*-explicit*)

    bool     has_value() const noexcept { return std::holds_alternative<T>(data_); }
    explicit operator bool() const noexcept { return has_value(); }

    T&       value() { return std::get<T>(data_); }
    const T& value() const { return std::get<T>(data_); }

    E&       error() { return std::get<E>(data_); }
    const E& error() const { return std::get<E>(data_); }

    T&       operator*() { return value(); }
    const T& operator*() const { return value(); }

    T*       operator->() { return &value(); }
    const T* operator->() const { return &value(); }

  private:
    std::variant<T, E> data_;
};

} // namespace nomos::rt

#endif
