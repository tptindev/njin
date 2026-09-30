#include "render_lod.h"

#include <algorithm>
#include <cmath>

namespace sandtable::city {

namespace {

view_cull state_cull;
view_stats stats;

// The chunk's own square, grown by how far a building or prop centred in it
// can stick out.
constexpr f32 overhang = 90.0f;

void chunk_rect(i32 c, f32 &x0, f32 &y0, f32 &x1, f32 &y1) {
  const i32 cx = c % state_cull.cols, cy = c / state_cull.cols;
  x0 = static_cast<f32>(cx) * chunk_size;
  y0 = static_cast<f32>(cy) * chunk_size;
  x1 = x0 + chunk_size;
  y1 = y0 + chunk_size;
}

// How far `p` is from the chunk's square (0 inside it).
f32 chunk_distance(i32 c, vec2 p) {
  f32 x0, y0, x1, y1;
  chunk_rect(c, x0, y0, x1, y1);
  const f32 dx = std::max({x0 - p.x, 0.0f, p.x - x1});
  const f32 dy = std::max({y0 - p.y, 0.0f, p.y - y1});
  return std::sqrt(dx * dx + dy * dy);
}

} // namespace

const view_cull &cull() { return state_cull; }
const view_stats &view_last_stats() { return stats; }

i32 chunk_count() { return state_cull.cols * state_cull.rows; }

i32 chunk_of(vec2 p) {
  const i32 cx = std::clamp(static_cast<i32>(p.x / chunk_size), 0, std::max(0, state_cull.cols - 1));
  const i32 cy = std::clamp(static_cast<i32>(p.y / chunk_size), 0, std::max(0, state_cull.rows - 1));
  return cy * state_cull.cols + cx;
}

bool chunk_visible(i32 c) {
  f32 x0, y0, x1, y1;
  chunk_rect(c, x0, y0, x1, y1);
  return x1 + overhang >= state_cull.x0 && x0 - overhang <= state_cull.x1 && y1 + overhang >= state_cull.y0 &&
         y0 - overhang <= state_cull.y1;
}

bool chunk_detailed(i32 c, f32 radius) { return radius > 0.0f && chunk_distance(c, state_cull.center) < radius; }

bool view_sees(vec2 p, f32 margin) {
  return p.x + margin >= state_cull.x0 && p.x - margin <= state_cull.x1 && p.y + margin >= state_cull.y0 &&
         p.y - margin <= state_cull.y1;
}

void count_detailed(i32 chunks) { stats.detailed = chunks; }

void chunk_grid(const city_map &map) {
  state_cull.cols = static_cast<i32>(std::ceil(map.desc.width / chunk_size));
  state_cull.rows = static_cast<i32>(std::ceil(map.desc.height / chunk_size));
}

void view_cull_update(context &ctx, const city_map &map, const view_options &opt) {
  view_cull &v = state_cull;
  chunk_grid(map);

  // The table seen: each corner and edge-middle of the screen cast as a ray
  // onto the street (height 0) and onto the tallest roofs; what lies between
  // the hits is in view. A ray over the horizon counts as reaching the far
  // plane.
  const camera3d cam = table_camera();
  const vec2 scr = screen_size(ctx);
  const f32 heights[] = {0.0f, 300.0f * unit3d};
  v.x0 = v.y0 = 1e30f;
  v.x1 = v.y1 = -1e30f;
  for (const vec2 s : {vec2{0, 0}, vec2{scr.x, 0}, vec2{0, scr.y}, scr, vec2{scr.x * 0.5f, 0},
                       vec2{scr.x * 0.5f, scr.y}, vec2{0, scr.y * 0.5f}, vec2{scr.x, scr.y * 0.5f}})
    for (const f32 h : heights) {
      const ray3d r = camera3d_ray(ctx, cam, s);
      vec3 hit;
      if (r.direction.y < -1e-4f && (r.origin.y - h) / -r.direction.y < cam.far_plane)
        hit = r.origin + r.direction * ((r.origin.y - h) / -r.direction.y);
      else
        hit = r.origin + r.direction * cam.far_plane;
      const vec2 p{hit.x / unit3d, hit.z / unit3d};
      v.x0 = std::min(v.x0, p.x);
      v.y0 = std::min(v.y0, p.y);
      v.x1 = std::max(v.x1, p.x);
      v.y1 = std::max(v.y1, p.y);
    }

  // Detail: close up everything near the middle of the view; from afar,
  // windows and stools are specks and are left out.
  const f32 d = state.cam_distance;
  v.focused = opt.focused;
  if (opt.focused) {
    v.center = opt.focus;
    v.detail_r = opt.focus_radius;
    v.prop_r = opt.focus_radius * 1.2f;
  } else {
    v.center = state.cam_target;
    v.detail_r = d < 35.0f ? 750.0f : d < 60.0f ? 420.0f : 0.0f;
    v.prop_r = d < 45.0f ? 800.0f : d < 70.0f ? 450.0f : 0.0f;
  }

  stats.chunks = chunk_count();
  stats.visible = 0;
  for (i32 c = 0; c < stats.chunks; ++c)
    stats.visible += chunk_visible(c) ? 1 : 0;
  stats.instances = 0;
}

view_cull view_cull_around(vec2 at, f32 reach) {
  const view_cull was = state_cull;
  state_cull.x0 = at.x - reach;
  state_cull.y0 = at.y - reach;
  state_cull.x1 = at.x + reach;
  state_cull.y1 = at.y + reach;
  state_cull.center = at;
  state_cull.detail_r = reach;
  state_cull.prop_r = reach;
  state_cull.focused = false;
  return was;
}

void view_cull_restore(const view_cull &was) { state_cull = was; }

std::vector<std::pair<u32, u32>> chunk_ranges(const chunked &b, const std::vector<u8> &want, const skip_list *skip) {
  std::vector<std::pair<u32, u32>> out;
  for (i32 c = 0; c < static_cast<i32>(want.size()); ++c) {
    if (!want[static_cast<size_t>(c)])
      continue;
    const u32 from = b.start[static_cast<size_t>(c)], to = b.start[static_cast<size_t>(c) + 1];
    if (from >= to)
      continue;
    if (!out.empty() && out.back().second == from)
      out.back().second = to;
    else
      out.emplace_back(from, to);
  }
  if (!skip || skip->empty())
    return out;
  // Cut the skipped spans out of the ranges.
  std::vector<std::pair<u32, u32>> cut;
  for (auto [from, to] : out) {
    u32 at = from;
    for (const auto &[s0, s1] : *skip) {
      if (s1 <= at || s0 >= to)
        continue;
      if (s0 > at)
        cut.emplace_back(at, s0);
      at = std::max(at, s1);
    }
    if (at < to)
      cut.emplace_back(at, to);
  }
  return cut;
}

void draw_ranges(context &ctx, const chunked &b, mesh3d_kind mesh, const std::vector<std::pair<u32, u32>> &ranges) {
  for (const auto &[from, to] : ranges) {
    b.inst.draw_range(ctx, mesh, from, to);
    stats.instances += to - from;
  }
}

void draw_ranges_model(context &ctx, const chunked &b, model_handle model,
                       const std::vector<std::pair<u32, u32>> &ranges) {
  for (const auto &[from, to] : ranges) {
    const u32 end = std::min(to, b.inst.count());
    if (end > from)
      draw_instanced3d(ctx, model, b.inst.buffer, from, end - from);
    stats.instances += end - from;
  }
}

} // namespace sandtable::city
