#include "njin_procgen.h"
#include "_random.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numeric>

namespace njin {
namespace {

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------

// Integer hash of a lattice point and a seed (lowbias32-style mixing), so every
// seed gives an unrelated field and results match on every machine.
u32 hash3(i32 x, i32 y, u32 seed) {
  u32 h = seed * 0x9E3779B9u;
  h ^= (u32)x * 0x85EBCA6Bu;
  h = (h ^ (h >> 15)) * 0x2C1B3C6Du;
  h ^= (u32)y * 0xC2B2AE35u;
  h = (h ^ (h >> 16)) * 0x297A2D39u;
  h ^= h >> 15;
  return h;
}

f32 fade(f32 t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

// Gradient noise in about [-1, 1]. Gradients are the 8 compass directions.
f32 perlin(f32 x, f32 y, u32 seed) {
  const f32 fx = std::floor(x), fy = std::floor(y);
  const i32 ix = (i32)fx, iy = (i32)fy;
  const f32 dx = x - fx, dy = y - fy;
  auto grad = [seed](i32 gx, i32 gy, f32 px, f32 py) {
    constexpr f32 d = 0.70710678f;
    switch (hash3(gx, gy, seed) & 7u) {
    case 0: return px;
    case 1: return -px;
    case 2: return py;
    case 3: return -py;
    case 4: return (px + py) * d;
    case 5: return (px - py) * d;
    case 6: return (-px + py) * d;
    default: return (-px - py) * d;
    }
  };
  const f32 n00 = grad(ix, iy, dx, dy);
  const f32 n10 = grad(ix + 1, iy, dx - 1.0f, dy);
  const f32 n01 = grad(ix, iy + 1, dx, dy - 1.0f);
  const f32 n11 = grad(ix + 1, iy + 1, dx - 1.0f, dy - 1.0f);
  const f32 u = fade(dx), v = fade(dy);
  const f32 a = n00 + u * (n10 - n00);
  const f32 b = n01 + u * (n11 - n01);
  // Unit gradients peak at sqrt(2)/2 on a 2D lattice: scale to [-1, 1].
  return std::clamp((a + v * (b - a)) * 1.41421356f, -1.0f, 1.0f);
}

// Value noise in [-1, 1].
f32 value_noise(f32 x, f32 y, u32 seed) {
  const f32 fx = std::floor(x), fy = std::floor(y);
  const i32 ix = (i32)fx, iy = (i32)fy;
  auto v = [seed](i32 gx, i32 gy) { return (f32)(hash3(gx, gy, seed) >> 8) * (2.0f / 16777216.0f) - 1.0f; };
  const f32 u = fade(x - fx), w = fade(y - fy);
  const f32 a = v(ix, iy) + u * (v(ix + 1, iy) - v(ix, iy));
  const f32 b = v(ix, iy + 1) + u * (v(ix + 1, iy + 1) - v(ix, iy + 1));
  return a + w * (b - a);
}

f32 base_noise(noise_type type, f32 x, f32 y, u32 seed) {
  return type == noise_value ? value_noise(x, y, seed) : perlin(x, y, seed);
}

// Fractal sum without warping, result in [0, 1].
f32 fractal(const noise_desc &d, f32 x, f32 y, u32 seed) {
  const i32 octaves = std::clamp(d.octaves, 1, 8);
  const noise_fractal kind = d.fractal == fractal_none ? fractal_fbm : d.fractal;
  const i32 count = d.fractal == fractal_none ? 1 : octaves;
  f32 freq = d.frequency, amp = 1.0f, sum = 0.0f, norm = 0.0f;
  for (i32 o = 0; o < count; o++) {
    const f32 n = base_noise(d.type, x * freq, y * freq, seed + (u32)o * 1013u);
    f32 layer;
    switch (kind) {
    case fractal_ridged: {
      const f32 r = 1.0f - std::fabs(n);
      layer = r * r; // sharpen the crests
      break;
    }
    case fractal_billow: layer = std::fabs(n); break;
    default: layer = n * 0.5f + 0.5f; break;
    }
    sum += layer * amp;
    norm += amp;
    freq *= d.lacunarity;
    amp *= d.gain;
  }
  return std::clamp(sum / norm, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Regions
// ---------------------------------------------------------------------------

// Labels the 4-connected regions of cells for which `member(value)` holds.
// Returns one label per cell (-1 for non-members) and the size of each region.
template <class F> std::vector<i32> label_regions(const tile_grid &g, F member, bool same_value,
                                                   std::vector<i32> &sizes) {
  std::vector<i32> label(g.cells.size(), -1);
  std::vector<i32> stack;
  sizes.clear();
  for (i32 start = 0; start < (i32)g.cells.size(); start++) {
    if (label[(usize)start] >= 0 || !member(g.cells[(usize)start]))
      continue;
    const i32 id = (i32)sizes.size();
    const i32 value = g.cells[(usize)start];
    i32 size = 0;
    label[(usize)start] = id;
    stack.push_back(start);
    while (!stack.empty()) {
      const i32 c = stack.back();
      stack.pop_back();
      size++;
      const i32 cx = c % g.width, cy = c / g.width;
      const i32 nx[4] = {cx + 1, cx - 1, cx, cx};
      const i32 ny[4] = {cy, cy, cy + 1, cy - 1};
      for (i32 k = 0; k < 4; k++) {
        if (!g.inside(nx[k], ny[k]))
          continue;
        const i32 n = ny[k] * g.width + nx[k];
        const i32 v = g.cells[(usize)n];
        if (label[(usize)n] >= 0 || !member(v) || (same_value && v != value))
          continue;
        label[(usize)n] = id;
        stack.push_back(n);
      }
    }
    sizes.push_back(size);
  }
  return label;
}

i32 keep_largest(tile_grid &g, const std::vector<i32> &walkable, i32 replace) {
  auto member = [&](i32 v) { return std::find(walkable.begin(), walkable.end(), v) != walkable.end(); };
  std::vector<i32> sizes;
  const std::vector<i32> label = label_regions(g, member, false, sizes);
  if (sizes.size() < 2)
    return 0;
  const i32 keep = (i32)(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
  i32 changed = 0;
  for (usize i = 0; i < g.cells.size(); i++)
    if (label[i] >= 0 && label[i] != keep) {
      g.cells[i] = replace;
      changed++;
    }
  return changed;
}

// Stretches values so the smallest is 0 and the largest 1.
void stretch(std::vector<f32> &v) {
  if (v.empty())
    return;
  const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
  const f32 a = *lo, span = *hi - *lo;
  if (span <= 1e-6f)
    return;
  for (f32 &x : v)
    x = (x - a) / span;
}

// ---------------------------------------------------------------------------
// WFC helpers
// ---------------------------------------------------------------------------

void wfc_resize(wfc_rules &r, i32 count) {
  const i32 words = std::max(1, (count + 63) / 64);
  if (words == r.words && (i32)r.allow.size() == count * 4 * words)
    return;
  std::vector<u64> next((usize)count * 4 * (usize)words, 0);
  const i32 old = (i32)r.allow.size() / std::max(1, 4 * std::max(r.words, 1));
  for (i32 i = 0; i < std::min(old, count); i++)
    for (i32 d = 0; d < 4; d++)
      for (i32 w = 0; w < std::min(words, r.words); w++)
        next[(usize)((i * 4 + d) * words + w)] = r.allow[(usize)((i * 4 + d) * r.words + w)];
  r.allow = std::move(next);
  r.words = words;
}

i32 wfc_index(const wfc_rules &r, i32 tile) {
  const auto it = std::find(r.tiles.begin(), r.tiles.end(), tile);
  return it == r.tiles.end() ? -1 : (i32)(it - r.tiles.begin());
}

constexpr i32 opposite(i32 dir) { return (dir + 2) % 4; }
constexpr i32 dir_dx[4] = {1, 0, -1, 0};
constexpr i32 dir_dy[4] = {0, 1, 0, -1};

} // namespace

// ---------------------------------------------------------------------------
// Grid
// ---------------------------------------------------------------------------

tile_grid tile_grid_make(i32 width, i32 height, i32 fill) {
  tile_grid g;
  g.width = std::max(width, 0);
  g.height = std::max(height, 0);
  g.cells.assign((usize)g.width * (usize)g.height, fill);
  return g;
}

tile_grid tile_grid_from_text(std::string_view text, std::initializer_list<tile_key> legend) {
  std::vector<std::string_view> rows;
  usize pos = text.starts_with("\r\n") ? 2 : (text.starts_with('\n') ? 1 : 0);
  while (pos < text.size()) {
    usize end = text.find('\n', pos);
    if (end == std::string_view::npos)
      end = text.size();
    std::string_view row = text.substr(pos, end - pos);
    if (!row.empty() && row.back() == '\r')
      row.remove_suffix(1);
    rows.push_back(row);
    pos = end + 1;
  }
  i32 width = 0;
  for (const std::string_view r : rows)
    width = std::max(width, (i32)r.size());
  tile_grid g = tile_grid_make(width, (i32)rows.size(), -1);
  for (i32 y = 0; y < g.height; y++)
    for (i32 x = 0; x < (i32)rows[(usize)y].size(); x++)
      for (const tile_key &k : legend)
        if (k.symbol == rows[(usize)y][(usize)x]) {
          g.set(x, y, k.tile);
          break;
        }
  return g;
}

void tilemap_from_grid(tilemap &map, const tile_grid &grid, cell origin) {
  for (i32 y = 0; y < grid.height; y++)
    for (i32 x = 0; x < grid.width; x++) {
      const i32 v = grid.get(x, y);
      if (v >= 0)
        tilemap_set(map, origin.x + x, origin.y + y, v);
    }
}

i32 grid_count(const tile_grid &grid, i32 tile) {
  return (i32)std::count(grid.cells.begin(), grid.cells.end(), tile);
}

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------

f32 noise_2d(const noise_desc &desc, f32 x, f32 y) {
  if (desc.warp != 0.0f) {
    noise_desc w = desc;
    w.frequency = desc.warp_frequency;
    w.octaves = 2;
    w.fractal = fractal_fbm;
    const f32 ox = fractal(w, x, y, desc.seed + 7717u) * 2.0f - 1.0f;
    const f32 oy = fractal(w, x, y, desc.seed + 9929u) * 2.0f - 1.0f;
    x += ox * desc.warp;
    y += oy * desc.warp;
  }
  return fractal(desc, x, y, desc.seed);
}

f32 noise_1d(const noise_desc &desc, f32 x) {
  // A row of the 2D field, away from the lattice lines so it never goes flat.
  return noise_2d(desc, x, 0.37f / std::max(desc.frequency, 1e-6f));
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

void grid_majority(tile_grid &grid, i32 iterations, i32 threshold) {
  for (i32 it = 0; it < iterations; it++) {
    tile_grid next = grid;
    for (i32 y = 0; y < grid.height; y++)
      for (i32 x = 0; x < grid.width; x++) {
        i32 values[9], counts[9], n = 0;
        for (i32 dy = -1; dy <= 1; dy++)
          for (i32 dx = -1; dx <= 1; dx++) {
            if (!grid.inside(x + dx, y + dy))
              continue;
            const i32 v = grid.get(x + dx, y + dy);
            i32 k = 0;
            while (k < n && values[k] != v)
              k++;
            if (k == n) {
              values[n] = v;
              counts[n++] = 0;
            }
            counts[k]++;
          }
        const i32 self = grid.get(x, y);
        i32 best = -1;
        for (i32 k = 0; k < n; k++)
          if (best < 0 || counts[k] > counts[best] || (counts[k] == counts[best] && values[k] == self))
            best = k;
        if (best >= 0 && counts[best] >= threshold)
          next.set(x, y, values[best]);
      }
    grid = std::move(next);
  }
}

void grid_smooth(tile_grid &grid, i32 solid, i32 open, i32 iterations, i32 threshold) {
  for (i32 it = 0; it < iterations; it++) {
    tile_grid next = grid;
    for (i32 y = 0; y < grid.height; y++)
      for (i32 x = 0; x < grid.width; x++) {
        const i32 v = grid.get(x, y);
        if (v != solid && v != open)
          continue;
        i32 walls = 0;
        for (i32 dy = -1; dy <= 1; dy++)
          for (i32 dx = -1; dx <= 1; dx++)
            if ((dx != 0 || dy != 0) && (!grid.inside(x + dx, y + dy) || grid.get(x + dx, y + dy) != open))
              walls++;
        next.set(x, y, walls >= threshold ? solid : open);
      }
    grid = std::move(next);
  }
}

i32 grid_remove_small(tile_grid &grid, i32 tile, i32 min_size, i32 replace) {
  std::vector<i32> sizes;
  const std::vector<i32> label = label_regions(grid, [tile](i32 v) { return v == tile; }, true, sizes);
  i32 removed = 0;
  for (const i32 s : sizes)
    removed += s < min_size ? 1 : 0;
  for (usize i = 0; i < grid.cells.size(); i++)
    if (label[i] >= 0 && sizes[(usize)label[i]] < min_size)
      grid.cells[i] = replace;
  return removed;
}

i32 grid_merge_small(tile_grid &grid, i32 min_size) {
  std::vector<i32> sizes;
  const std::vector<i32> label = label_regions(grid, [](i32) { return true; }, true, sizes);
  // Cells of each small region, then the most common tile around it.
  std::vector<std::vector<i32>> members(sizes.size());
  for (usize i = 0; i < grid.cells.size(); i++)
    if (sizes[(usize)label[i]] < min_size)
      members[(usize)label[i]].push_back((i32)i);
  i32 merged = 0;
  for (usize r = 0; r < members.size(); r++) {
    if (members[r].empty())
      continue;
    std::vector<std::pair<i32, i32>> around; // tile, count
    for (const i32 c : members[r]) {
      const i32 cx = c % grid.width, cy = c / grid.width;
      const i32 nx[4] = {cx + 1, cx - 1, cx, cx};
      const i32 ny[4] = {cy, cy, cy + 1, cy - 1};
      for (i32 k = 0; k < 4; k++) {
        if (!grid.inside(nx[k], ny[k]) || label[(usize)(ny[k] * grid.width + nx[k])] == (i32)r)
          continue;
        const i32 v = grid.get(nx[k], ny[k]);
        auto it = std::find_if(around.begin(), around.end(), [v](const auto &p) { return p.first == v; });
        if (it == around.end())
          around.push_back({v, 1});
        else
          it->second++;
      }
    }
    if (around.empty())
      continue;
    const i32 tile = std::max_element(around.begin(), around.end(),
                                      [](const auto &a, const auto &b) { return a.second < b.second; })
                         ->first;
    for (const i32 c : members[r])
      grid.cells[(usize)c] = tile;
    merged++;
  }
  return merged;
}

i32 grid_keep_largest(tile_grid &grid, std::initializer_list<i32> walkable, i32 replace) {
  return keep_largest(grid, std::vector<i32>(walkable), replace);
}

i32 grid_border(tile_grid &grid, i32 tile, i32 touching, i32 replace, u8 sides, i32 distance) {
  std::vector<u8> frontier(grid.cells.size(), 0), marked(grid.cells.size(), 0);
  for (usize i = 0; i < grid.cells.size(); i++)
    frontier[i] = grid.cells[i] == touching ? 1 : 0;
  // side_up means "the cell above is touching": look from the candidate towards that side.
  const i32 sdx[4] = {0, 0, -1, 1}, sdy[4] = {-1, 1, 0, 0};
  const u8 bits[4] = {side_up, side_down, side_left, side_right};
  for (i32 step = 0; step < distance; step++) {
    std::vector<u8> next(grid.cells.size(), 0);
    bool any = false;
    for (i32 y = 0; y < grid.height; y++)
      for (i32 x = 0; x < grid.width; x++) {
        const usize i = (usize)(y * grid.width + x);
        if (grid.cells[i] != tile || marked[i])
          continue;
        for (i32 k = 0; k < 4; k++) {
          if (!(sides & bits[k]) || !grid.inside(x + sdx[k], y + sdy[k]))
            continue;
          if (frontier[(usize)((y + sdy[k]) * grid.width + x + sdx[k])]) {
            next[i] = 1;
            marked[i] = 1;
            any = true;
            break;
          }
        }
      }
    frontier = std::move(next);
    if (!any)
      break;
  }
  i32 changed = 0;
  for (usize i = 0; i < grid.cells.size(); i++)
    if (marked[i]) {
      grid.cells[i] = replace;
      changed++;
    }
  return changed;
}

i32 grid_scatter(tile_grid &grid, i32 place, f32 chance, u32 seed,
                 const std::function<bool(const tile_grid &, i32, i32)> &where, i32 min_distance) {
  rng r(seed);
  std::vector<i32> order(grid.cells.size());
  std::iota(order.begin(), order.end(), 0);
  for (usize i = order.size(); i > 1; i--)
    std::swap(order[i - 1], order[(usize)r.range(0, (i32)i - 1)]);
  std::vector<u8> placed(grid.cells.size(), 0);
  i32 count = 0;
  for (const i32 c : order) {
    const i32 x = c % grid.width, y = c / grid.width;
    if (!where(grid, x, y) || r.unit() >= chance)
      continue;
    bool clear = true;
    for (i32 dy = -min_distance + 1; dy < min_distance && clear; dy++)
      for (i32 dx = -min_distance + 1; dx < min_distance && clear; dx++)
        if (grid.inside(x + dx, y + dy) && placed[(usize)((y + dy) * grid.width + x + dx)])
          clear = false;
    if (!clear)
      continue;
    grid.set(x, y, place);
    placed[(usize)c] = 1;
    count++;
  }
  return count;
}

i32 grid_scatter(tile_grid &grid, i32 on, i32 place, f32 chance, u32 seed, i32 min_distance) {
  return grid_scatter(
      grid, place, chance, seed, [on](const tile_grid &g, i32 x, i32 y) { return g.get(x, y) == on; }, min_distance);
}

// ---------------------------------------------------------------------------
// Autotile
// ---------------------------------------------------------------------------

namespace {
// A diagonal neighbour only matters when both orthogonal neighbours beside it are joined: otherwise the
// corner is already hidden by an edge. What is left are the 47 blob masks.
u8 reduce_blob(u8 mask) {
  if (!((mask & neighbor_up) && (mask & neighbor_right)))
    mask &= (u8)~neighbor_up_right;
  if (!((mask & neighbor_right) && (mask & neighbor_down)))
    mask &= (u8)~neighbor_down_right;
  if (!((mask & neighbor_down) && (mask & neighbor_left)))
    mask &= (u8)~neighbor_down_left;
  if (!((mask & neighbor_left) && (mask & neighbor_up)))
    mask &= (u8)~neighbor_up_left;
  return mask;
}

// Rank of every reduced mask among the reduced masks, ascending; 256 entries, unused ones -1.
const std::array<i32, 256> &blob_table() {
  static const std::array<i32, 256> table = [] {
    std::array<i32, 256> t{};
    t.fill(-1);
    i32 next = 0;
    for (i32 m = 0; m < 256; m++)
      if (reduce_blob((u8)m) == m)
        t[(usize)m] = next++;
    return t;
  }();
  return table;
}
} // namespace

i32 autotile_count(autotile_layout layout) { return layout == autotile_edges ? 16 : 47; }

i32 autotile_index(u8 mask, autotile_layout layout) {
  if (layout == autotile_edges)
    return ((mask & neighbor_up) ? 1 : 0) | ((mask & neighbor_right) ? 2 : 0) | ((mask & neighbor_down) ? 4 : 0) |
           ((mask & neighbor_left) ? 8 : 0);
  return blob_table()[reduce_blob(mask)];
}

i32 grid_autotile(tile_grid &grid, const std::vector<autotile_rule> &rules) {
  const tile_grid src = grid; // every rule reads the grid as it was, never its own output
  static constexpr i32 dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  static constexpr i32 dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
  static constexpr u8 bit[8] = {neighbor_up,   neighbor_up_right,   neighbor_right,     neighbor_down_right,
                                neighbor_down, neighbor_down_left, neighbor_left,      neighbor_up_left};
  i32 changed = 0;
  for (i32 y = 0; y < src.height; y++)
    for (i32 x = 0; x < src.width; x++) {
      const i32 here = src.get(x, y);
      for (const autotile_rule &rule : rules) {
        const auto has = [](const std::vector<i32> &v, i32 t) { return std::find(v.begin(), v.end(), t) != v.end(); };
        if (!has(rule.tiles, here))
          continue;
        u8 mask = 0;
        for (i32 k = 0; k < 8; k++) {
          const i32 nx = x + dx[k], ny = y + dy[k];
          bool joined;
          if (src.inside(nx, ny)) {
            const i32 t = src.get(nx, ny);
            joined = has(rule.tiles, t) || has(rule.joins, t);
          } else {
            // outside the grid: joined only if every side it lies beyond is listed in `outside`
            joined = (nx >= 0 || (rule.outside & side_left)) && (nx < src.width || (rule.outside & side_right)) &&
                     (ny >= 0 || (rule.outside & side_up)) && (ny < src.height || (rule.outside & side_down));
          }
          if (joined)
            mask |= bit[k];
        }
        grid.set(x, y, rule.base + autotile_index(mask, rule.layout));
        changed++;
        break;
      }
    }
  return changed;
}

// ---------------------------------------------------------------------------
// Top-down
// ---------------------------------------------------------------------------

topdown_gen_result generate_topdown(const topdown_gen_desc &desc) {
  topdown_gen_result out;
  out.grid = tile_grid_make(desc.width, desc.height, -1);
  tile_grid &g = out.grid;
  out.heights.assign(g.cells.size(), 0.0f);
  bool moisture = false;
  for (const biome &b : desc.biomes)
    moisture = moisture || b.max_moisture < 1.0f;
  const f32 island = std::clamp(desc.island, 0.0f, 1.0f);

  std::vector<f32> wet(moisture ? g.cells.size() : 0, 0.0f);
  for (i32 y = 0; y < g.height; y++)
    for (i32 x = 0; x < g.width; x++) {
      f32 h = noise_2d(desc.height_noise, (f32)x, (f32)y);
      if (island > 0.0f) {
        // 0 at the centre, 1 on every edge (a rounded square), so the sea surrounds the land.
        const f32 nx = g.width > 1 ? (f32)x / (f32)(g.width - 1) * 2.0f - 1.0f : 0.0f;
        const f32 ny = g.height > 1 ? (f32)y / (f32)(g.height - 1) * 2.0f - 1.0f : 0.0f;
        const f32 edge = 1.0f - (1.0f - nx * nx) * (1.0f - ny * ny);
        h *= 1.0f - island * edge;
      }
      out.heights[(usize)(y * g.width + x)] = h;
      if (moisture)
        wet[(usize)(y * g.width + x)] = noise_2d(desc.moisture_noise, (f32)x, (f32)y);
    }
  if (desc.normalize) {
    stretch(out.heights);
    stretch(wet);
  }
  for (usize i = 0; i < g.cells.size(); i++) {
    const f32 h = out.heights[i], m = moisture ? wet[i] : 0.0f;
    i32 tile = desc.biomes.empty() ? -1 : desc.biomes.back().tile;
    for (const biome &b : desc.biomes)
      if (h <= b.max_height && m <= b.max_moisture) {
        tile = b.tile;
        break;
      }
    g.cells[i] = tile;
  }

  if (desc.smooth > 0)
    grid_majority(g, desc.smooth);
  if (desc.min_region > 1)
    grid_merge_small(g, desc.min_region);
  if (desc.border_tile >= 0)
    for (i32 y = 0; y < g.height; y++)
      for (i32 x = 0; x < g.width; x++)
        if (x == 0 || y == 0 || x == g.width - 1 || y == g.height - 1)
          g.set(x, y, desc.border_tile);
  if (!desc.walkable.empty())
    keep_largest(g, desc.walkable, desc.blocked_tile);

  // Spawn: the walkable cell nearest the centre.
  out.spawn = {g.width / 2, g.height / 2};
  if (!desc.walkable.empty()) {
    i64 best = -1;
    for (i32 y = 0; y < g.height; y++)
      for (i32 x = 0; x < g.width; x++) {
        if (std::find(desc.walkable.begin(), desc.walkable.end(), g.get(x, y)) == desc.walkable.end())
          continue;
        const i64 d = (i64)(x - g.width / 2) * (x - g.width / 2) + (i64)(y - g.height / 2) * (y - g.height / 2);
        if (best < 0 || d < best) {
          best = d;
          out.spawn = {x, y};
        }
      }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Platformer
// ---------------------------------------------------------------------------

platformer_gen_result generate_platformer(const platformer_gen_desc &desc) {
  platformer_gen_result out;
  const i32 w = std::max(desc.width, 1), h = std::max(desc.height, 3);
  out.grid = tile_grid_make(w, h, -1);
  tile_grid &g = out.grid;
  std::vector<i32> &s = out.surface;
  s.assign((usize)w, 0);
  rng r(desc.surface_noise.seed * 2654435761u + 17u);

  const i32 lo = std::clamp(std::min(desc.ground_min, desc.ground_max), 1, h - 1);
  const i32 hi = std::clamp(std::max(desc.ground_min, desc.ground_max), 1, h - 1);
  for (i32 x = 0; x < w; x++)
    s[(usize)x] = std::clamp((i32)std::lround((f32)lo + noise_1d(desc.surface_noise, (f32)x) * (f32)(hi - lo)), lo, hi);

  // Pits, away from both ends, with solid ground between two pits.
  const i32 safe = std::clamp(desc.safe_columns, 1, w / 2);
  std::vector<u8> pit((usize)w, 0);
  if (desc.pit_chance > 0.0f) {
    const i32 pmin = std::max(desc.pit_min, 1), pmax = std::max(desc.pit_max, pmin);
    for (i32 x = safe + 1; x < w - safe - 1;) {
      if (r.unit() < desc.pit_chance) {
        const i32 len = r.range(pmin, pmax);
        if (x + len >= w - safe - 1)
          break;
        for (i32 k = 0; k < len; k++)
          pit[(usize)(x + k)] = 1;
        x += len + std::max(3, pmin); // room to land and take off again
      } else {
        x++;
      }
    }
  }

  // Climbable: each column within max_step of the previous solid one; the column
  // after a pit is level with the one before it, so every pit is a level jump. The two columns
  // before a pit are level too: a step down right at the edge leaves no ground to take off from.
  const i32 step = std::max(desc.max_step, 0);
  i32 prev = s[0];
  bool after_pit = false;
  for (i32 x = 0; x < w; x++) {
    if (pit[(usize)x]) {
      after_pit = true;
      continue;
    }
    const bool run_up = (x + 1 < w && pit[(usize)(x + 1)]) || (x + 2 < w && pit[(usize)(x + 2)]);
    if (x > 0)
      s[(usize)x] = (after_pit || run_up) ? prev : std::clamp(s[(usize)x], prev - step, prev + step);
    prev = s[(usize)x];
    after_pit = false;
  }
  for (i32 x = 0; x < safe; x++)
    s[(usize)x] = s[(usize)safe];
  for (i32 x = w - safe; x < w; x++)
    s[(usize)x] = s[(usize)(w - safe - 1)];

  // Ground, dirt, deep rock.
  for (i32 x = 0; x < w; x++) {
    if (pit[(usize)x])
      continue;
    for (i32 y = s[(usize)x]; y < h; y++) {
      const i32 depth = y - s[(usize)x];
      i32 tile = depth == 0 ? desc.surface_tile : desc.dirt_tile;
      if (depth > 0 && desc.deep_tile >= 0 && depth >= desc.deep_depth)
        tile = desc.deep_tile;
      g.set(x, y, tile);
    }
  }

  // One-tile rises and falls become slopes.
  if (desc.slope_r_tile >= 0 || desc.slope_l_tile >= 0) {
    for (i32 x = 1; x < w - 1; x++) {
      if (pit[(usize)x])
        continue;
      const i32 here = s[(usize)x];
      const bool rise = !pit[(usize)(x - 1)] && s[(usize)(x - 1)] == here + 1;  // left is one lower
      const bool fall = !pit[(usize)(x + 1)] && s[(usize)(x + 1)] == here + 1;  // right is one lower
      if (rise && !fall && desc.slope_r_tile >= 0)
        g.set(x, here, desc.slope_r_tile);
      else if (fall && !rise && desc.slope_l_tile >= 0)
        g.set(x, here, desc.slope_l_tile);
    }
  }

  // Caves: `caves` is the share of the eligible underground cells to hollow out. Ranking the cave noise
  // over exactly those cells and taking a band around its median gives winding tunnels of that share,
  // whatever the noise' own range (fBm values cluster near 0.5, so a fixed threshold cannot do this).
  if (desc.caves > 0.0f) {
    struct candidate { f32 value; i32 x, y; };
    std::vector<candidate> pool;
    for (i32 x = 0; x < w; x++) {
      if (pit[(usize)x])
        continue;
      for (i32 y = s[(usize)x] + std::max(desc.cave_margin, 1); y < h - 1; y++)
        pool.push_back({noise_2d(desc.cave_noise, (f32)x, (f32)y), x, y});
    }
    std::sort(pool.begin(), pool.end(), [](const candidate &a, const candidate &b) { return a.value < b.value; });
    const f32 share = std::clamp(desc.caves, 0.0f, 0.9f);
    const usize lo = (usize)((0.5f - share * 0.5f) * (f32)pool.size());
    const usize hi = (usize)((0.5f + share * 0.5f) * (f32)pool.size());
    for (usize i = lo; i < hi && i < pool.size(); i++)
      g.set(pool[i].x, pool[i].y, -1);
    const auto rock_at = [&](i32 x, i32 y) {
      return desc.deep_tile >= 0 && y - s[(usize)x] >= desc.deep_depth ? desc.deep_tile : desc.dirt_tile;
    };
    // Round the tunnels off: two cellular-automaton passes (the classic cave rule) over the hollowed cells
    // only. Pit walls and the border count as rock, so a cave never opens into a pit or out of the level.
    for (i32 pass = 0; pass < 2; pass++) {
      std::vector<candidate> flips;
      for (const candidate &c : pool) {
        i32 solid = 0;
        for (i32 dy = -1; dy <= 1; dy++)
          for (i32 dx = -1; dx <= 1; dx++)
            if ((dx != 0 || dy != 0) && (!g.inside(c.x + dx, c.y + dy) || pit[(usize)(c.x + dx)] || g.get(c.x + dx, c.y + dy) != -1))
              solid++;
        const bool open = g.get(c.x, c.y) == -1;
        if (open && solid > 4)
          flips.push_back({0.0f, c.x, c.y}); // an open cell closes
        else if (!open && solid < 4)
          flips.push_back({1.0f, c.x, c.y}); // a rock cell opens
      }
      for (const candidate &f : flips)
        g.set(f.x, f.y, f.value > 0.5f ? -1 : rock_at(f.x, f.y));
    }
    // No pockets of a few cells: they read as noise, not as caves.
    std::vector<i32> sizes;
    const std::vector<i32> label = label_regions(g, [](i32 v) { return v == -1; }, true, sizes);
    for (usize i = 0; i < g.cells.size(); i++)
      if (label[i] >= 0 && sizes[(usize)label[i]] < 8) {
        const i32 y = (i32)i / w, x = (i32)i % w;
        g.cells[i] = rock_at(x, y);
      }
  }

  // Floating platforms, at most platform_height above the ground below them.
  if (desc.platform_chance > 0.0f && desc.platform_tile >= 0) {
    const i32 pmin = std::max(desc.platform_min, 1), pmax = std::max(desc.platform_max, pmin);
    for (i32 x = safe; x < w - safe;) {
      if (r.unit() >= desc.platform_chance) {
        x++;
        continue;
      }
      const i32 len = std::min(r.range(pmin, pmax), w - safe - x);
      i32 ground = h;
      for (i32 k = 0; k < len; k++)
        if (!pit[(usize)(x + k)])
          ground = std::min(ground, s[(usize)(x + k)]);
      if (ground < h) {
        const i32 row = ground - std::max(desc.platform_height, 2);
        if (row >= 1)
          for (i32 k = 0; k < len; k++)
            if (g.get(x + k, row) == -1)
              g.set(x + k, row, desc.platform_tile);
      }
      x += len + 3;
    }
  }

  const i32 sx = safe / 2, gx = w - 1 - safe / 2;
  out.spawn = {sx, s[(usize)sx] - 1};
  out.goal = {gx, s[(usize)gx] - 1};
  for (i32 x = 0; x < w; x++)
    if (pit[(usize)x])
      s[(usize)x] = -1;
  return out;
}

// ---------------------------------------------------------------------------
// Wave Function Collapse
// ---------------------------------------------------------------------------

i32 wfc_add_tile(wfc_rules &rules, i32 tile, f32 weight) {
  const i32 i = wfc_index(rules, tile);
  if (i >= 0) {
    rules.weights[(usize)i] = weight;
    return i;
  }
  rules.tiles.push_back(tile);
  rules.weights.push_back(weight);
  wfc_resize(rules, (i32)rules.tiles.size());
  return (i32)rules.tiles.size() - 1;
}

void wfc_allow(wfc_rules &rules, i32 a, wfc_dir dir, i32 b) {
  i32 ia = wfc_index(rules, a);
  if (ia < 0)
    ia = wfc_add_tile(rules, a);
  i32 ib = wfc_index(rules, b);
  if (ib < 0)
    ib = wfc_add_tile(rules, b);
  const i32 w = rules.words;
  rules.allow[(usize)((ia * 4 + dir) * w + ib / 64)] |= 1ull << (ib % 64);
  rules.allow[(usize)((ib * 4 + opposite(dir)) * w + ia / 64)] |= 1ull << (ia % 64);
}

wfc_rules wfc_learn(const tile_grid &sample, bool periodic) {
  wfc_rules rules;
  std::vector<f32> counts;
  for (const i32 v : sample.cells) {
    const i32 i = wfc_index(rules, v);
    if (i < 0) {
      wfc_add_tile(rules, v, 1.0f);
      counts.push_back(1.0f);
    } else {
      counts[(usize)i] += 1.0f;
    }
  }
  rules.weights = counts;
  for (i32 y = 0; y < sample.height; y++)
    for (i32 x = 0; x < sample.width; x++) {
      const i32 a = sample.get(x, y);
      for (const wfc_dir d : {wfc_right, wfc_down}) {
        i32 nx = x + dir_dx[d], ny = y + dir_dy[d];
        if (periodic) {
          nx = (nx + sample.width) % sample.width;
          ny = (ny + sample.height) % sample.height;
        } else if (!sample.inside(nx, ny)) {
          continue;
        }
        wfc_allow(rules, a, d, sample.get(nx, ny));
      }
    }
  return rules;
}

bool wfc_generate(const wfc_rules &rules, const wfc_desc &desc, tile_grid &out) {
  const i32 T = (i32)rules.tiles.size();
  const i32 W = std::max(desc.width, 0), H = std::max(desc.height, 0);
  const i32 N = W * H;
  const i32 words = rules.words;
  if (T == 0 || N == 0 || (i32)rules.allow.size() < T * 4 * words)
    return false;

  std::vector<f32> weight((usize)T), wlogw((usize)T);
  for (i32 t = 0; t < T; t++) {
    weight[(usize)t] = std::max(rules.weights[(usize)t], 1e-6f);
    wlogw[(usize)t] = weight[(usize)t] * std::log(weight[(usize)t]);
  }
  std::vector<u64> full((usize)words, 0);
  for (i32 t = 0; t < T; t++)
    full[(usize)(t / 64)] |= 1ull << (t % 64);

  // The per-cell constraint mask never changes between attempts.
  std::vector<u64> start((usize)N * (usize)words);
  for (i32 c = 0; c < N; c++)
    for (i32 k = 0; k < words; k++)
      start[(usize)(c * words + k)] = full[(usize)k];
  if (desc.allowed)
    for (i32 c = 0; c < N; c++)
      for (i32 t = 0; t < T; t++)
        if (!desc.allowed(c % W, c / W, rules.tiles[(usize)t]))
          start[(usize)(c * words + t / 64)] &= ~(1ull << (t % 64));

  rng r(desc.seed);
  std::vector<u64> dom;
  std::vector<i32> count((usize)N);
  std::vector<f32> sum_w((usize)N), sum_wlogw((usize)N), entropy((usize)N), tie((usize)N);
  std::vector<i32> stack;
  std::vector<u64> support((usize)words);

  auto refresh = [&](i32 c) {
    i32 n = 0;
    f32 sw = 0.0f, swl = 0.0f;
    for (i32 k = 0; k < words; k++) {
      u64 bits = dom[(usize)(c * words + k)];
      n += std::popcount(bits);
      while (bits) {
        const i32 t = k * 64 + std::countr_zero(bits);
        bits &= bits - 1;
        sw += weight[(usize)t];
        swl += wlogw[(usize)t];
      }
    }
    count[(usize)c] = n;
    sum_w[(usize)c] = sw;
    sum_wlogw[(usize)c] = swl;
    entropy[(usize)c] = n > 1 ? std::log(sw) - swl / sw + tie[(usize)c] : 0.0f;
  };

  // Removes from every neighbour the tiles that no tile left in `c` supports.
  auto propagate = [&]() -> bool {
    while (!stack.empty()) {
      const i32 c = stack.back();
      stack.pop_back();
      const i32 cx = c % W, cy = c / W;
      for (i32 d = 0; d < 4; d++) {
        i32 nx = cx + dir_dx[d], ny = cy + dir_dy[d];
        if (desc.periodic) {
          nx = (nx + W) % W;
          ny = (ny + H) % H;
        } else if (nx < 0 || ny < 0 || nx >= W || ny >= H) {
          continue;
        }
        const i32 n = ny * W + nx;
        std::fill(support.begin(), support.end(), 0);
        for (i32 k = 0; k < words; k++) {
          u64 bits = dom[(usize)(c * words + k)];
          while (bits) {
            const i32 t = k * 64 + std::countr_zero(bits);
            bits &= bits - 1;
            const u64 *a = &rules.allow[(usize)((t * 4 + d) * words)];
            for (i32 j = 0; j < words; j++)
              support[(usize)j] |= a[j];
          }
        }
        bool changed = false, empty = true;
        for (i32 k = 0; k < words; k++) {
          u64 &nb = dom[(usize)(n * words + k)];
          const u64 next = nb & support[(usize)k];
          changed = changed || next != nb;
          nb = next;
          empty = empty && next == 0;
        }
        if (empty)
          return false;
        if (changed) {
          refresh(n);
          stack.push_back(n);
        }
      }
    }
    return true;
  };

  for (i32 attempt = 0; attempt < std::max(desc.attempts, 1); attempt++) {
    dom = start;
    bool ok = true;
    stack.clear();
    for (f32 &n : tie)
      n = r.unit() * 1e-4f; // breaks ties between equal cells, differently on every attempt
    for (i32 c = 0; c < N; c++) {
      refresh(c);
      if (count[(usize)c] == 0)
        return false; // a cell nothing may occupy: no attempt can fix that
      stack.push_back(c);
    }
    ok = propagate();
    while (ok) {
      // Observe the undecided cell with the least entropy.
      i32 pick = -1;
      f32 best = 0.0f;
      for (i32 c = 0; c < N; c++) {
        if (count[(usize)c] <= 1)
          continue;
        const f32 e = entropy[(usize)c];
        if (pick < 0 || e < best) {
          pick = c;
          best = e;
        }
      }
      if (pick < 0)
        break; // every cell decided
      // Weighted choice among the tiles still possible; the last one catches rounding.
      f32 roll = r.unit() * sum_w[(usize)pick];
      i32 chosen = -1;
      bool done = false;
      for (i32 k = 0; k < words && !done; k++) {
        u64 bits = dom[(usize)(pick * words + k)];
        while (bits && !done) {
          const i32 t = k * 64 + std::countr_zero(bits);
          bits &= bits - 1;
          chosen = t;
          roll -= weight[(usize)t];
          done = roll <= 0.0f;
        }
      }
      for (i32 k = 0; k < words; k++)
        dom[(usize)(pick * words + k)] = 0;
      dom[(usize)(pick * words + chosen / 64)] = 1ull << (chosen % 64);
      refresh(pick);
      stack.push_back(pick);
      ok = propagate();
    }
    if (!ok)
      continue;
    tile_grid g = tile_grid_make(W, H, -1);
    for (i32 c = 0; c < N; c++)
      for (i32 k = 0; k < words; k++)
        if (dom[(usize)(c * words + k)]) {
          g.cells[(usize)c] = rules.tiles[(usize)(k * 64 + std::countr_zero(dom[(usize)(c * words + k)]))];
          break;
        }
    out = std::move(g);
    return true;
  }
  return false;
}

} // namespace njin
