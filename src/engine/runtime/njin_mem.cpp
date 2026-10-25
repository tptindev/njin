// The game-facing side of the memory manager. The allocator itself is
// src/engine/mem/njin_memcore.cpp, inside the raylib target.
#include "njin_mem.h"
#include "njin_cfg.h"
#include "njin_log.h"
#include "njin_mem_impl.h"
#include "njin_memcore.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace njin {
namespace {
struct hook {
  u32 id;
  mem_pressure_fn fn;
};
std::vector<hook> &hooks() {
  static std::vector<hook> h;
  return h;
}
u32 next_id = 1;
bool armed = true;                       // false between a soft-limit firing and used falling back
std::atomic<bool> asked{false};          // mem_pressure()
constexpr usize mb = 1024 * 1024;

mem_stats to_stats(const njin_mem_snapshot &s) {
  return mem_stats{.used = s.used,
                   .peak = s.peak,
                   .soft_limit = s.soft,
                   .hard_limit = s.hard,
                   .new_bytes = s.group_used[0],
                   .raylib_bytes = s.group_used[1],
                   .allocs = s.allocs,
                   .frees = s.frees,
                   .refused = s.refused};
}

std::string fmt_mb(usize bytes) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f MB", (double)bytes / (double)mb);
  return buf;
}
} // namespace

mem_stats mem_info() {
  njin_mem_snapshot s;
  njin_mem_get(&s);
  return to_stats(s);
}

void mem_set_limit(usize soft_bytes, usize hard_bytes) {
  njin_mem_set_limits(soft_bytes, hard_bytes);
  armed = true;
}

u32 mem_on_pressure(mem_pressure_fn fn) {
  const u32 id = next_id++;
  hooks().push_back({id, std::move(fn)});
  return id;
}

void mem_off_pressure(u32 id) {
  auto &h = hooks();
  h.erase(std::remove_if(h.begin(), h.end(), [id](const hook &x) { return x.id == id; }), h.end());
}

void mem_pressure(context &) { asked.store(true); }

void mem_reset_peak() { njin_mem_reset_peak(); }

void mem_apply_config(const config &cfg) {
  if (cfg.mem_budget_mb == 0 && cfg.mem_limit_mb == 0)
    return;
  const usize soft = cfg.mem_budget_mb * mb;
  usize hard = cfg.mem_limit_mb * mb;
  if (hard != 0 && soft > hard)
    NJIN_WARN("config::mem_budget_mb (%zu) is above mem_limit_mb (%zu): callbacks would never run in time",
              (size_t)cfg.mem_budget_mb, (size_t)cfg.mem_limit_mb);
  mem_set_limit(soft, hard);
}

void mem_frame(context &ctx) {
  njin_mem_snapshot snap;
  njin_mem_get(&snap);
  const bool crossed = njin_mem_take_pressure() != 0;
  if (snap.soft != 0 && snap.used < snap.soft / 10 * 9)
    armed = true;
  const bool manual = asked.exchange(false);
  if (!manual && !(crossed && armed && snap.soft != 0))
    return;
  if (!manual) {
    armed = false;
    NJIN_WARN("memory: %s in use is past the soft limit of %s (%s hard); releasing caches", fmt_mb(snap.used).c_str(),
              fmt_mb(snap.soft).c_str(), snap.hard != 0 ? fmt_mb(snap.hard).c_str() : "no");
  }
  // A callback may register or drop callbacks: walk a copy.
  const std::vector<hook> copy = hooks();
  const mem_stats stats = to_stats(snap);
  for (const hook &h : copy)
    h.fn(ctx, stats);
  if (manual) {
    njin_mem_get(&snap);
    NJIN_INFO("memory: pressure callbacks ran, %s in use", fmt_mb(snap.used).c_str());
  }
}
} // namespace njin
