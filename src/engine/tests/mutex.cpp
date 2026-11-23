#include "njin_mutex.h"

#include <cstdlib>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

using njin::detail::mutex;
using njin::detail::mutex_guard;

static void check(bool ok) {
  if (!ok)
    std::abort();
}

static_assert(!std::is_copy_constructible_v<mutex<int>>);
static_assert(!std::is_move_constructible_v<mutex<int>>);
static_assert(!std::is_copy_constructible_v<mutex_guard<int>>);
static_assert(std::is_nothrow_move_constructible_v<mutex_guard<int>>);
static_assert(std::is_same_v<decltype(*std::declval<const mutex<int> &>().lock()), const int &>);

int main() {
  mutex<int> counter;
  check(*counter.lock() == 0);
  std::vector<std::thread> workers;
  for (int t = 0; t < 8; ++t)
    workers.emplace_back([&] {
      for (int i = 0; i < 25000; ++i) {
        auto guard = counter.lock();
        ++*guard;
      }
    });
  for (auto &worker : workers)
    worker.join();
  check(*counter.lock() == 200000);

  // Contention is checked from another thread: recursively trying a
  // non-recursive mutex on its owning thread is undefined behavior.
  {
    auto guard = counter.lock();
    auto contender = std::async(std::launch::async, [&] { return !counter.try_lock().has_value(); });
    check(contender.get());
    auto moved = std::move(guard);
    check(*moved == 200000);
  }
  auto acquired = counter.try_lock();
  check(acquired.has_value());
  acquired.reset();

  try {
    auto guard = counter.lock();
    *guard = 42;
    throw std::runtime_error("unwind");
  } catch (const std::runtime_error &) {
  }
  const auto &read_only = counter;
  auto read = read_only.try_lock();
  check(read.has_value() && **read == 42);
  static_assert(std::is_same_v<decltype(**read), const int &>);
  read.reset();

  mutex<int> other(std::in_place, 7);
  {
    auto first = counter.lock();
    auto second = other.lock();
    first = std::move(second); // releases counter and takes other's lock
    check(*first == 7);
    auto contender = std::async(std::launch::async, [&] {
      auto free = counter.try_lock();
      return free.has_value() && !other.try_lock().has_value();
    });
    check(contender.get());
  }

  mutex<std::unique_ptr<int>> owned(std::in_place, std::make_unique<int>(9));
  check(**owned.lock() == 9);
}
