#pragma once
#include "njin_internal_only.h"

#include <cassert>
#include <mutex>
#include <optional>
#include <utility>

namespace njin::detail {
template <class T> class mutex;

// A lock and access to its value travel together. Like std::unique_lock, a
// guard must be destroyed on the thread that acquired the lock. The owning
// mutex must outlive every guard; references must not escape the guard's scope.
template <class T> class mutex_guard {
  template <class U> friend class mutex;
  std::unique_lock<std::mutex> lock_;
  T *value_;

  mutex_guard(T &value, std::unique_lock<std::mutex> lock) noexcept
      : lock_(std::move(lock)), value_(&value) {}

public:
  mutex_guard(const mutex_guard &) = delete;
  mutex_guard &operator=(const mutex_guard &) = delete;
  mutex_guard(mutex_guard &&) noexcept = default;
  mutex_guard &operator=(mutex_guard &&) noexcept = default;

  T &operator*() const {
    assert(lock_.owns_lock());
    return *value_;
  }
  T *operator->() const { return &**this; }
};

// Rust-style owning mutex, implemented with standard C++ RAII. No unguarded
// data accessor, copying or moving: guards always refer to a stable owner.
template <class T> class mutex {
  mutable std::mutex mutex_;
  T value_{};

public:
  mutex() = default;
  template <class... Args>
  explicit mutex(std::in_place_t, Args &&...args) : value_(std::forward<Args>(args)...) {}
  mutex(const mutex &) = delete;
  mutex &operator=(const mutex &) = delete;
  mutex(mutex &&) = delete;
  mutex &operator=(mutex &&) = delete;

  [[nodiscard]] mutex_guard<T> lock() & { return {value_, std::unique_lock(mutex_)}; }
  [[nodiscard]] mutex_guard<const T> lock() const & { return {value_, std::unique_lock(mutex_)}; }
  mutex_guard<T> lock() && = delete;
  mutex_guard<const T> lock() const && = delete;

  [[nodiscard]] std::optional<mutex_guard<T>> try_lock() & {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
      return std::nullopt;
    return mutex_guard<T>(value_, std::move(lock));
  }
  [[nodiscard]] std::optional<mutex_guard<const T>> try_lock() const & {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock())
      return std::nullopt;
    return mutex_guard<const T>(value_, std::move(lock));
  }
  std::optional<mutex_guard<T>> try_lock() && = delete;
  std::optional<mutex_guard<const T>> try_lock() const && = delete;
};
} // namespace njin::detail
