#include "njin_spatial_batch.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace njin {
namespace {
bool finite(vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool cell_bounds(const batch_grid2d &g, i32 cell, vec2 &lo, vec2 &hi) {
  if (cell < 0 || cell >= g.count()) return false;
  lo = {g.origin.x + (cell % g.cols) * g.cell_size.x,
        g.origin.y + (cell / g.cols) * g.cell_size.y};
  hi = {lo.x + g.cell_size.x, lo.y + g.cell_size.y};
  return finite(lo) && finite(hi);
}
}
i32 batch_grid2d::count() const {
  if (cols <= 0 || rows <= 0 || cell_size.x <= 0 || cell_size.y <= 0 ||
      !finite(origin) || !finite(cell_size) ||
      static_cast<i64>(cols) * rows > std::numeric_limits<i32>::max()) return 0;
  return cols * rows;
}
i32 batch_grid2d::cell_at(vec2 p) const {
  if (!count() || !finite(p)) return -1;
  const auto at = [](f32 p0, f32 origin0, f32 size, i32 n) {
    const f64 value = (static_cast<f64>(p0) - origin0) / size;
    return static_cast<i32>(std::clamp(std::floor(value), 0.0, static_cast<f64>(n - 1)));
  };
  return at(p.y, origin.y, cell_size.y, rows) * cols + at(p.x, origin.x, cell_size.x, cols);
}
bool batch_cell_visible(const batch_grid2d &g, const batch_view2d &v, i32 cell) {
  vec2 lo, hi;
  if (!cell_bounds(g, cell, lo, hi) || !finite(v.lo) || !finite(v.hi) ||
      v.lo.x > v.hi.x || v.lo.y > v.hi.y || !std::isfinite(v.overhang) || v.overhang < 0) return false;
  return hi.x + v.overhang >= v.lo.x && lo.x - v.overhang <= v.hi.x &&
         hi.y + v.overhang >= v.lo.y && lo.y - v.overhang <= v.hi.y;
}
bool batch_cell_detailed(const batch_grid2d &g, const batch_view2d &v, i32 cell, f32 radius) {
  vec2 lo, hi;
  if (!cell_bounds(g, cell, lo, hi) || !finite(v.focus) || !std::isfinite(radius) || radius <= 0) return false;
  const f64 dx = std::max({static_cast<f64>(lo.x) - v.focus.x, 0.0, static_cast<f64>(v.focus.x) - hi.x});
  const f64 dy = std::max({static_cast<f64>(lo.y) - v.focus.y, 0.0, static_cast<f64>(v.focus.y) - hi.y});
  return dx * dx + dy * dy < static_cast<f64>(radius) * radius;
}
std::vector<instance_range> batch_instance_ranges(std::span<const u32> offsets,
    std::span<const u8> wanted, std::span<const instance_range> excluded) {
  std::vector<instance_range> visible;
  if (offsets.size() != wanted.size() + 1 || !std::is_sorted(offsets.begin(), offsets.end())) return visible;
  for (size_t c = 0; c < wanted.size(); ++c) {
    const u32 from = offsets[c], to = offsets[c + 1];
    if (!wanted[c] || from == to) continue;
    if (!visible.empty() && visible.back().second == from) visible.back().second = to;
    else visible.emplace_back(from, to);
  }
  if (excluded.empty() || visible.empty()) return visible;
  std::vector<instance_range> skips;
  for (const auto &[from, to] : excluded) {
    const u32 end = std::min(to, offsets.back());
    if (from < end) skips.emplace_back(from, end);
  }
  std::sort(skips.begin(), skips.end());
  std::vector<instance_range> merged;
  for (const auto &s : skips) {
    if (!merged.empty() && merged.back().second >= s.first)
      merged.back().second = std::max(merged.back().second, s.second);
    else merged.push_back(s);
  }
  std::vector<instance_range> out;
  size_t k = 0;
  for (const auto &[from, to] : visible) {
    u32 at = from;
    while (k < merged.size() && merged[k].second <= at) ++k;
    for (size_t j = k; j < merged.size() && merged[j].first < to; ++j) {
      const auto &[s0, s1] = merged[j];
      if (s0 > at) out.emplace_back(at, s0);
      at = std::max(at, s1);
    }
    if (at < to) out.emplace_back(at, to);
  }
  return out;
}
} // namespace njin
