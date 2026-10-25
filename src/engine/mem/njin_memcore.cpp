// See njin_memcore.h. Every block carries a 16-byte header with its size, its
// group and how far the user pointer sits from what malloc returned, so free
// needs nothing from the caller and over-aligned blocks work the same way.
#include "njin_memcore.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
struct alignas(16) header {
  size_t size;
  uint32_t offset;
  uint32_t group;
};
static_assert(sizeof(header) == 16);

constexpr size_t default_align = alignof(header);
constexpr uint32_t group_new = 0;
constexpr uint32_t group_raylib = 1;

// Constant-initialised, so allocations before main() are counted too.
std::atomic<size_t> g_used{0};
std::atomic<size_t> g_peak{0};
std::atomic<size_t> g_soft{0};
std::atomic<size_t> g_hard{0};
std::atomic<size_t> g_group[NJIN_MEM_GROUPS];
std::atomic<unsigned long long> g_allocs{0};
std::atomic<unsigned long long> g_frees{0};
std::atomic<unsigned long long> g_refused{0};
std::atomic<size_t> g_bucket_count[NJIN_MEM_BUCKETS];
std::atomic<size_t> g_bucket_bytes[NJIN_MEM_BUCKETS];
std::atomic<bool> g_pressure{false};

size_t bucket_of(size_t n) {
  size_t b = 0;
  size_t cap = 16;
  while (b + 1 < NJIN_MEM_BUCKETS && n > cap) {
    cap <<= 1;
    b++;
  }
  return b;
}

// Takes `n` bytes against the limits. False when the hard limit says no.
bool reserve(size_t n) {
  const size_t now = g_used.fetch_add(n, std::memory_order_relaxed) + n;
  const size_t hard = g_hard.load(std::memory_order_relaxed);
  if (hard != 0 && now > hard) {
    g_used.fetch_sub(n, std::memory_order_relaxed);
    g_refused.fetch_add(1, std::memory_order_relaxed);
    g_pressure.store(true, std::memory_order_relaxed);
    return false;
  }
  const size_t soft = g_soft.load(std::memory_order_relaxed);
  if (soft != 0 && now > soft)
    g_pressure.store(true, std::memory_order_relaxed);
  size_t peak = g_peak.load(std::memory_order_relaxed);
  while (now > peak && !g_peak.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {
  }
  return true;
}

void *track_alloc(size_t n, size_t align, uint32_t group) {
  const size_t pad = align > alignof(header) ? align : 0;
  if (n > SIZE_MAX - sizeof(header) - pad)
    return nullptr;
  if (!reserve(n))
    return nullptr;
  void *raw = std::malloc(n + sizeof(header) + pad);
  if (raw == nullptr) {
    g_used.fetch_sub(n, std::memory_order_relaxed);
    return nullptr;
  }
  uintptr_t user = reinterpret_cast<uintptr_t>(raw) + sizeof(header);
  if (pad != 0)
    user = (user + align - 1) & ~(uintptr_t)(align - 1);
  header *h = reinterpret_cast<header *>(user) - 1;
  h->size = n;
  h->offset = (uint32_t)(user - reinterpret_cast<uintptr_t>(raw));
  h->group = group;
  g_group[group].fetch_add(n, std::memory_order_relaxed);
  const size_t b = bucket_of(n);
  g_bucket_count[b].fetch_add(1, std::memory_order_relaxed);
  g_bucket_bytes[b].fetch_add(n, std::memory_order_relaxed);
  g_allocs.fetch_add(1, std::memory_order_relaxed);
  return reinterpret_cast<void *>(user);
}

void track_free(void *p) {
  if (p == nullptr)
    return;
  header *h = static_cast<header *>(p) - 1;
  const size_t n = h->size;
  const size_t b = bucket_of(n);
  g_used.fetch_sub(n, std::memory_order_relaxed);
  g_group[h->group].fetch_sub(n, std::memory_order_relaxed);
  g_bucket_count[b].fetch_sub(1, std::memory_order_relaxed);
  g_bucket_bytes[b].fetch_sub(n, std::memory_order_relaxed);
  g_frees.fetch_add(1, std::memory_order_relaxed);
  std::free(static_cast<char *>(p) - h->offset);
}

[[noreturn]] void out_of_memory(size_t n) {
  std::fprintf(stderr,
               "njin: out of memory. A %zu byte allocation would take the game past its hard limit "
               "(%zu of %zu bytes in use). Raise config::mem_limit_mb, or free more in the "
               "mem_on_pressure() callbacks.\n",
               n, g_used.load(), g_hard.load());
  std::fflush(stderr);
  std::abort();
}

void *new_or_throw(size_t n, size_t align) {
  for (;;) {
    if (void *p = track_alloc(n == 0 ? 1 : n, align, group_new))
      return p;
    std::new_handler h = std::get_new_handler();
    if (h == nullptr)
      throw std::bad_alloc();
    h();
  }
}

void *new_or_null(size_t n, size_t align) noexcept {
  try {
    return new_or_throw(n, align);
  } catch (...) {
    return nullptr;
  }
}
} // namespace

extern "C" {
void *njin_rl_malloc(size_t n) {
  void *p = track_alloc(n == 0 ? 1 : n, alignof(header), group_raylib);
  if (p == nullptr)
    out_of_memory(n);
  return p;
}
void *njin_rl_calloc(size_t count, size_t size) {
  if (size != 0 && count > SIZE_MAX / size)
    return nullptr;
  void *p = njin_rl_malloc(count * size);
  std::memset(p, 0, count * size);
  return p;
}
void *njin_rl_realloc(void *p, size_t n) {
  if (p == nullptr)
    return njin_rl_malloc(n);
  if (n == 0) {
    njin_rl_free(p);
    return nullptr;
  }
  const header *h = static_cast<header *>(p) - 1;
  const size_t old = h->size;
  void *q = njin_rl_malloc(n);
  std::memcpy(q, p, old < n ? old : n);
  njin_rl_free(p);
  return q;
}
void njin_rl_free(void *p) { track_free(p); }

void njin_mem_get(njin_mem_snapshot *out) {
  out->used = g_used.load();
  out->peak = g_peak.load();
  out->soft = g_soft.load();
  out->hard = g_hard.load();
  for (int i = 0; i < NJIN_MEM_GROUPS; i++)
    out->group_used[i] = g_group[i].load();
  out->allocs = g_allocs.load();
  out->frees = g_frees.load();
  out->refused = g_refused.load();
  for (int i = 0; i < NJIN_MEM_BUCKETS; i++) {
    out->bucket_count[i] = g_bucket_count[i].load();
    out->bucket_bytes[i] = g_bucket_bytes[i].load();
  }
}
void njin_mem_set_limits(size_t soft, size_t hard) {
  g_soft.store(soft);
  g_hard.store(hard);
}
int njin_mem_take_pressure(void) { return g_pressure.exchange(false) ? 1 : 0; }
void njin_mem_reset_peak(void) { g_peak.store(g_used.load()); }
}

// Replacing the global operators puts the engine, the standard library and the
// game's own new/delete under the same count.
void *operator new(size_t n) { return new_or_throw(n, default_align); }
void *operator new[](size_t n) { return new_or_throw(n, default_align); }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return new_or_null(n, default_align); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return new_or_null(n, default_align); }
void *operator new(size_t n, std::align_val_t a) { return new_or_throw(n, (size_t)a); }
void *operator new[](size_t n, std::align_val_t a) { return new_or_throw(n, (size_t)a); }
void *operator new(size_t n, std::align_val_t a, const std::nothrow_t &) noexcept { return new_or_null(n, (size_t)a); }
void *operator new[](size_t n, std::align_val_t a, const std::nothrow_t &) noexcept { return new_or_null(n, (size_t)a); }

void operator delete(void *p) noexcept { track_free(p); }
void operator delete[](void *p) noexcept { track_free(p); }
void operator delete(void *p, size_t) noexcept { track_free(p); }
void operator delete[](void *p, size_t) noexcept { track_free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { track_free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { track_free(p); }
void operator delete(void *p, std::align_val_t) noexcept { track_free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { track_free(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { track_free(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept { track_free(p); }
void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept { track_free(p); }
void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept { track_free(p); }
