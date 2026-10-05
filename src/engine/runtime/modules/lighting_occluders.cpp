#include "lighting_internal.h"
#include "_comps.h"
#include "njin_ctx_impl.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>
#include "njin_camera.h"

namespace njin::light_impl {
namespace {
// A transform with its rotation worked out once, applied to many points.
struct placement {
  vec2 pos;
  f32 scale, cos_r, sin_r;

  explicit placement(const transform &tr) : pos(tr.pos), scale(tr.scale), cos_r(1.0f), sin_r(0.0f) {
    if (tr.rot != 0.0f) {
      const f32 r = tr.rot * (PI / 180.0f);
      cos_r = std::cos(r), sin_r = std::sin(r);
    }
  }

  vec2 operator()(vec2 p) const {
    const f32 x = p.x * scale, y = p.y * scale;
    return {pos.x + x * cos_r - y * sin_r, pos.y + x * sin_r + y * cos_r};
  }
};

// Collects the edges of the occluders in the world. Each occluder is built into
// `pending` first, and kept only if some of it is near the view.
struct edge_builder {
  std::vector<occluder_edge> &edges;
  const std::vector<rect> &areas; // where the lights reach
  std::vector<occluder_edge> pending;

  // True when a box touches the reach of some light: only those shadow anything.
  bool relevant(f32 min_x, f32 min_y, f32 max_x, f32 max_y) const {
    for (const rect &a : areas) {
      if (overlaps(a, min_x, min_y, max_x, max_y))
        return true;
    }
    return false;
  }

  void begin() { pending.clear(); }

  void add(vec2 a, vec2 b, bool solid) {
    pending.push_back({a, b, std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y), 0, 0, solid});
  }

  // One loop or line in world coordinates. A closed loop becomes one sided
  // edges with the solid on their left (a hole the other way round); a line, or
  // a closed loop with no area, becomes walls: an edge each way.
  void shape(std::vector<vec2> &p, bool closed, bool hole) {
    const usize n = p.size();
    if (n < 2)
      return;
    if (closed && n >= 3) {
      const f32 area = signed_area(p);
      if (area != 0.0f) {
        if ((area > 0.0f) == hole)
          std::reverse(p.begin(), p.end());
        for (usize i = 0; i < n; i++)
          add(p[i], p[(i + 1) % n], true);
        return;
      }
    }
    const usize count = closed && n >= 3 ? n : n - 1;
    for (usize i = 0; i < count; i++) {
      add(p[i], p[(i + 1) % n], false);
      add(p[(i + 1) % n], p[i], false);
    }
  }

  void end() {
    bool any = false;
    for (const occluder_edge &e : pending)
      any = any || relevant(e.min_x, e.min_y, e.max_x, e.max_y);
    if (!any)
      return;
    const u32 first = (u32)edges.size(), count = (u32)pending.size();
    for (occluder_edge e : pending) {
      e.owner_first = first, e.owner_count = count;
      edges.push_back(e);
    }
  }
};

// Level of detail: drops the points of `p` that stray less than `tol` from the
// line through their neighbours that stay (Douglas-Peucker). A closed loop is
// cut at its first point and the point farthest from it, so it keeps both.
void keep_from(const std::vector<vec2> &p, usize a, usize b, f32 tol2, std::vector<u8> &keep) {
  const vec2 d = p[b] - p[a];
  const f32 len2 = d.x * d.x + d.y * d.y;
  f32 worst = 0.0f;
  usize at = a;
  for (usize i = a + 1; i < b; i++) {
    const vec2 q = p[i] - p[a];
    const f32 cross = q.x * d.y - q.y * d.x;
    const f32 dist2 = len2 > 1e-12f ? cross * cross / len2 : q.x * q.x + q.y * q.y;
    if (dist2 > worst)
      worst = dist2, at = i;
  }
  if (worst <= tol2)
    return;
  keep[at] = 1;
  keep_from(p, a, at, tol2, keep);
  keep_from(p, at, b, tol2, keep);
}

void simplify(std::vector<vec2> &p, bool closed, f32 tol, std::vector<vec2> &scratch, std::vector<u8> &keep) {
  const usize n = p.size();
  if (tol <= 0.0f || n < (closed ? 4u : 3u))
    return;
  scratch.assign(p.begin(), p.end());
  usize far = n - 1;
  if (closed) {
    f32 best = -1.0f;
    for (usize i = 1; i < n; i++) {
      const vec2 q = p[i] - p[0];
      if (q.x * q.x + q.y * q.y > best)
        best = q.x * q.x + q.y * q.y, far = i;
    }
    scratch.push_back(p[0]); // the loop back to the start, as a chain
  }
  keep.assign(scratch.size(), 0);
  keep.front() = keep[far] = keep.back() = 1;
  const f32 tol2 = tol * tol;
  keep_from(scratch, 0, far, tol2, keep);
  keep_from(scratch, far, scratch.size() - 1, tol2, keep);
  p.clear();
  const usize end = closed ? scratch.size() - 1 : scratch.size();
  for (usize i = 0; i < end; i++)
    if (keep[i] != 0)
      p.push_back(scratch[i]);
}

// The pixels of a texture, read back from the graphics card once.
const Image *pixels_of(lighting_state &s, const texture_slot &slot) {
  const auto it = s.images.find(slot.texture.id);
  if (it != s.images.end()) {
    if (it->second.version == slot.version)
      return &it->second.image;
    UnloadImage(it->second.image);
    s.images.erase(it);
    s.silhouettes.clear(); // traced from the old picture
  }
  Image image = LoadImageFromTexture(slot.texture);
  if (image.data == nullptr)
    return nullptr;
  if (image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  return &(s.images[slot.texture.id] = lighting_state::cpu_image{image, slot.version}).image;
}

// The outline of the frame (x, y, w, h in texture pixels) of `slot`, traced on
// first use and remembered.
const std::vector<silhouette_loop> *silhouette_of(lighting_state &s, const texture_slot &slot, i32 x, i32 y, i32 w,
                                                  i32 h, f32 alpha, f32 simplify) {
  u64 key = slot.texture.id;
  const auto mix = [&key](u64 v) { key ^= v + 0x9e3779b97f4a7c15ULL + (key << 6) + (key >> 2); };
  mix(slot.version), mix((u64)(u32)x), mix((u64)(u32)y), mix((u64)(u32)w), mix((u64)(u32)h);
  mix((u64)(alpha * 255.0f)), mix((u64)(simplify * 16.0f));
  const auto found = s.silhouettes.find(key);
  if (found != s.silhouettes.end())
    return &found->second;
  const Image *image = pixels_of(s, slot);
  if (image == nullptr)
    return nullptr;
  const Color *px = (const Color *)image->data;
  const u8 threshold = (u8)std::clamp(alpha * 255.0f, 1.0f, 255.0f);
  cell_list cells;
  for (i32 j = 0; j < h; j++) {
    for (i32 i = 0; i < w; i++) {
      const i32 sx = x + i, sy = y + j;
      if (sx >= 0 && sy >= 0 && sx < image->width && sy < image->height && px[sy * image->width + sx].a >= threshold)
        cells.push_back({i, j});
    }
  }
  return &(s.silhouettes[key] = outline_cells(cells, simplify));
}
} // namespace

vec4 directional_bins(const rect &view, f32 angle) {
  const vec2 across{-std::sin(angle), std::cos(angle)};
  f32 lo = 1e30f, hi = -1e30f;
  for (const vec2 c : {view.pos, vec2{view.pos.x + view.size.x, view.pos.y}, vec2{view.pos.x, view.pos.y + view.size.y},
                       view.pos + view.size}) {
    const f32 v = c.x * across.x + c.y * across.y;
    lo = std::min(lo, v), hi = std::max(hi, v);
  }
  return {across.x, across.y, lo, 32.0f / std::max(hi - lo, 1e-3f)};
}

// True when `p` is inside the solid a set of edges bounds (even-odd, so holes count).
bool inside_owner(const std::vector<occluder_edge> &edges, const occluder_edge &any_edge, vec2 p) {
  bool inside = false;
  for (u32 i = any_edge.owner_first; i < any_edge.owner_first + any_edge.owner_count; i++) {
    const occluder_edge &e = edges[i];
    if ((e.a.y > p.y) != (e.b.y > p.y) && p.x < (e.b.x - e.a.x) * (p.y - e.a.y) / (e.b.y - e.a.y) + e.a.x)
      inside = !inside;
  }
  return inside;
}

// Every occluder near the view as edges: the shapes of light_occluder, and the
// outlines of light_occluder_sprite frames.
void gather_edges(context &ctx, lighting_state &s, std::vector<occluder_edge> &edges, const std::vector<rect> &areas,
                  f32 lod) {
  entt::registry &registry = ctx.ecs.registry;
  edge_builder builder{edges, areas, {}};

  // Reused for every shape.
  static thread_local std::vector<vec2> world, scratch;
  static thread_local std::vector<u8> keep;
  for (auto [entity, tr, occ] : registry.view<const transform, const light_occluder>().each()) {
    if (occ.points.size() < 2)
      continue;
    // Far from the view: skip it before doing any work on its points. The
    // shape's own `reach` when it says one, else measured.
    f32 reach = occ.reach;
    if (reach <= 0.0f) {
      f32 reach2 = 0.0f;
      for (const vec2 &p : occ.points)
        reach2 = std::max(reach2, p.x * p.x + p.y * p.y);
      reach = std::sqrt(reach2);
    }
    reach *= std::abs(tr.scale);
    // Smaller on screen than the detail that may go: its shadow would be too.
    if (reach < lod)
      continue;
    if (!builder.relevant(tr.pos.x - reach, tr.pos.y - reach, tr.pos.x + reach, tr.pos.y + reach))
      continue;
    const placement place(tr);
    builder.begin();
    world.clear();
    for (const vec2 &p : occ.points)
      world.push_back(place(p));
    simplify(world, occ.closed, lod, scratch, keep);
    builder.shape(world, occ.closed, occ.hole);
    builder.end();
  }

  for (auto [entity, tr, spr, occ] : registry.view<const transform, const sprite, const light_occluder_sprite>().each()) {
    if (!spr.visible)
      continue;
    const texture_slot *slot = texture_slot_of(ctx.texture, spr.texture);
    if (slot == nullptr)
      continue;
    const Rectangle area = texture_area(*slot);
    const bool whole = spr.source.size.x == 0.0f || spr.source.size.y == 0.0f;
    const i32 w = (i32)(whole ? area.width : spr.source.size.x), h = (i32)(whole ? area.height : spr.source.size.y);
    const i32 x = (i32)(area.x + (whole ? 0.0f : spr.source.pos.x)), y = (i32)(area.y + (whole ? 0.0f : spr.source.pos.y));
    // Out of every light's reach: do not even trace it.
    const f32 span = (f32)std::max(w, h) * std::abs(tr.scale);
    if (span < 2.0f * lod)
      continue;
    if (!builder.relevant(tr.pos.x - span, tr.pos.y - span, tr.pos.x + span, tr.pos.y + span))
      continue;
    const std::vector<silhouette_loop> *loops = silhouette_of(s, *slot, x, y, w, h, occ.alpha, occ.simplify);
    if (loops == nullptr)
      continue;
    const placement place(tr);
    builder.begin();
    for (const silhouette_loop &loop : *loops) {
      world.clear();
      for (const vec2 &p : loop.points) {
        const f32 px = spr.flip_x ? (f32)w - p.x : p.x, py = spr.flip_y ? (f32)h - p.y : p.y;
        world.push_back(place({px - spr.origin.x * (f32)w, py - spr.origin.y * (f32)h}));
      }
      simplify(world, true, lod, scratch, keep);
      builder.shape(world, true, loop.hole);
    }
    builder.end();
  }
}
} // namespace njin::light_impl
