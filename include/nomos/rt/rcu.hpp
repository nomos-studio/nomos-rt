// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// _LGPL_SOURCE before any urcu header enables the inline read_lock/unlock
// fast-path.  Define it here so translation units that include <kairos/rcu.hpp>
// don't need to know about it.
#ifndef _LGPL_SOURCE
#define _LGPL_SOURCE
#endif

// urcu C headers use GNU extensions (named variadic macros, ## paste with empty
// __VA_ARGS__, volatile-qualified return types) that are invalid under
// -Wpedantic in C++ mode.  Suppress only these specific diagnostics for the
// duration of the urcu include and restore the caller's warning state after.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wvariadic-macros"
#pragma GCC diagnostic ignored "-Wdeprecated-volatile"
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
#include <urcu-bp.h>
#pragma GCC diagnostic pop
// urcu-bp.h already ends with <urcu/map/clear.h>; generic aliases stripped,
// urcu_bp_* symbols remain declared.

#include <atomic>
#include <memory>

// ---------------------------------------------------------------------------
// nomos::rt::rcu_managed<T>
//
// Wraps a heap-allocated T* behind an atomic pointer.  Two usage patterns:
//
//   Writer (control thread):
//       mgr.store(std::make_unique<T>(...));   // atomic swap + call_rcu retire
//
//   Reader (audio callback, lock-free fast path):
//       auto guard = mgr.read();               // RAII read-side critical section
//       guard->method();                        // T is stable for guard's lifetime
//
// The writer path uses call_rcu() (non-blocking) so the control thread is
// never stalled waiting for the audio callback to exit its read-side section.
//
// Forward-compatibility note: nomos::rt::rcu_detail wraps urcu_bp_* symbols so
// that a future port to std::rcu<> (C++26, once libstdc++ ships it) or
// urcu_mb_* (Linux thread-registration path) only needs changes here.
// ---------------------------------------------------------------------------

namespace nomos::rt::rcu_detail {

inline void read_lock() noexcept {
    urcu_bp_read_lock();
}
inline void read_unlock() noexcept {
    urcu_bp_read_unlock();
}

inline void call(struct rcu_head* head, void (*cb)(struct rcu_head*)) noexcept {
    urcu_bp_call_rcu(head, cb);
}

inline void synchronize() noexcept {
    urcu_bp_synchronize_rcu();
}

} // namespace nomos::rt::rcu_detail

namespace nomos::rt {

template <typename T> class rcu_managed {
  public:
    // Construct with an initial value.  Takes ownership.
    explicit rcu_managed(std::unique_ptr<T> initial = nullptr) noexcept : ptr_(initial.release()) {}

    ~rcu_managed() {
        // Drain any pending call_rcu callbacks before destroying.
        rcu_detail::synchronize();
        delete ptr_.load(std::memory_order_acquire);
    }

    rcu_managed(const rcu_managed&)            = delete;
    rcu_managed& operator=(const rcu_managed&) = delete;

    // -----------------------------------------------------------------------
    // reader_guard — RAII read-side critical section.
    //
    // Constructed on the audio thread; holds the pointer stable for its
    // lifetime.  Must not outlive the rcu_managed that produced it.
    // -----------------------------------------------------------------------
    class reader_guard {
      public:
        explicit reader_guard(const rcu_managed& owner) noexcept : ptr_(nullptr) {
            rcu_detail::read_lock();
            ptr_ = owner.ptr_.load(std::memory_order_acquire);
        }

        ~reader_guard() noexcept { rcu_detail::read_unlock(); }

        reader_guard(const reader_guard&)            = delete;
        reader_guard& operator=(const reader_guard&) = delete;

        T*       operator->() noexcept { return ptr_; }
        const T* operator->() const noexcept { return ptr_; }
        T&       operator*() noexcept { return *ptr_; }
        const T& operator*() const noexcept { return *ptr_; }

        explicit operator bool() const noexcept { return ptr_ != nullptr; }

      private:
        T* ptr_;
    };

    // Obtain a read-side guard.  Call from the audio thread.
    [[nodiscard]] reader_guard read() const noexcept { return reader_guard{*this}; }

    // -----------------------------------------------------------------------
    // store() — writer path (control thread).
    //
    // Atomically installs next as the new value and schedules the old pointer
    // for deferred reclamation via call_rcu().  Returns immediately; the old
    // T is destroyed once all concurrent readers have exited.
    // -----------------------------------------------------------------------
    void store(std::unique_ptr<T> next) noexcept {
        T* incoming = next.release();
        T* outgoing = ptr_.exchange(incoming, std::memory_order_release);
        if (!outgoing)
            return;

        // Embed the rcu_head inside a retire_node so call_rcu can reach it
        // without any allocation inside the callback.
        auto* node = new retire_node{outgoing};
        rcu_detail::call(&node->head, retire_cb);
    }

    // Pointer to current value without read-side lock — only safe when the
    // caller can guarantee no concurrent writers (e.g. single-threaded tests).
    T* unsafe_get() const noexcept { return ptr_.load(std::memory_order_relaxed); }

  private:
    // retire_node carries the outgoing pointer through call_rcu's callback.
    // It is allocated on the heap by store() and freed by retire_cb().
    struct retire_node {
        struct rcu_head head{}; // caa_container_of target — retire_node is standard-layout
        T*              ptr;

        explicit retire_node(T* p) noexcept : ptr(p) {}
    };

    static void retire_cb(struct rcu_head* head) noexcept {
        // caa_container_of is liburcu's container_of (uses offsetof);
        // retire_node must be standard-layout for offsetof to be well-defined.
        auto* node = caa_container_of(head, retire_node, head);
        delete node->ptr;
        delete node;
    }

    std::atomic<T*> ptr_;
};

} // namespace nomos::rt
