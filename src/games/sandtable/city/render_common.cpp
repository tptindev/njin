#include "render_lod.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>

// The city renderer's shared pieces, and view_build()/view_draw() which run
// the parts in order.

namespace sandtable::city {

u32 mix(u32 look, u32 salt) {
  u32 h = look ^ (salt * 0x9E3779B9u);
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

rgba district_color(district_kind k) {
  static const rgba c[] = {rgb8(214, 92, 72),  rgb8(232, 178, 64), rgb8(120, 168, 96), rgb8(196, 82, 170),
                           rgb8(70, 130, 196), rgb8(128, 112, 96), rgb8(90, 196, 190)};
  return c[static_cast<i32>(k)];
}

rgba business_color(business_kind k) {
  static const rgba c[] = {
      rgb8(150, 92, 52),  // cafe
      rgb8(232, 120, 40), // street food
      rgb8(200, 40, 40),  // restaurant
      rgb8(60, 150, 70),  // grocery
      rgb8(40, 170, 90),  // pharmacy
      rgb8(240, 196, 40), // gold
      rgb8(170, 30, 30),  // pawn
      rgb8(220, 50, 180), // karaoke
      rgb8(130, 60, 210), // bar
      rgb8(40, 120, 90),  // billiards
      rgb8(240, 110, 160),// massage
      rgb8(60, 110, 200), // guest house
      rgb8(40, 70, 170),  // hotel
      rgb8(90, 90, 100),  // bike repair
      rgb8(230, 60, 40),  // gas
      rgb8(210, 150, 60), // market
      rgb8(110, 120, 130),// warehouse
      rgb8(140, 100, 70), // workshop
      rgb8(30, 30, 30),   // gambling den
  };
  return c[static_cast<i32>(k)];
}

rgba ground_color(const city_map &map, const cell_info &c) {
  static const rgba district_ground[] = {rgb8(184, 172, 150), rgb8(186, 174, 148), rgb8(172, 162, 140),
                                         rgb8(166, 156, 150), rgb8(158, 156, 150), rgb8(148, 142, 132),
                                         rgb8(190, 190, 182)};
  switch (c.g) {
  case ground::water:
  case ground::bridge: return rgb8(46, 84, 100);
  case ground::park: return rgb8(92, 138, 70);
  case ground::plaza: return rgb8(196, 188, 170);
  case ground::lot: return rgb8(156, 136, 104);
  default: {
    const i32 dk = c.district < map.districts.size() ? static_cast<i32>(map.districts[c.district].kind) : 2;
    rgba g = district_ground[dk];
    if (c.block >= 0)
      g = shade(g, 0.96f + pick01(static_cast<u32>(c.block), 3) * 0.08f);
    return g;
  }
  }
}

// --- instances -----------------------------------------------------------------------

void instances::add3(vec3 p, vec3 s, rgba c, f32 yaw, f32 pitch) {
  data.insert(data.end(), {p.x, p.y, p.z, 1.0f, c.r, c.g, c.b, c.a, pitch, yaw, 0.0f, 0.0f, s.x, s.y, s.z, 0.0f});
}

void instances::box(vec2 at, f32 base, vec3 size, f32 angle, rgba col) {
  add3(to3d(at, (base + size.y * 0.5f) * unit3d), size * unit3d, col, -angle);
}

void instances::tilted(vec2 at, f32 mid, vec3 size, f32 angle, f32 pitch, rgba col) {
  add3(to3d(at, mid * unit3d), size * unit3d, col, -angle, pitch);
}

void instances::post(vec2 at, f32 base, f32 radius, f32 height, rgba col) {
  add3(to3d(at, base * unit3d), vec3{radius, height, radius} * unit3d, col);
}

void instances::ball(vec2 at, f32 lift, f32 radius, rgba col) {
  add3(to3d(at, lift * unit3d), vec3{radius, radius, radius} * unit3d, col);
}

void instances::upload(context &ctx) {
  if (buffer.id == 0)
    buffer = instance_buffer_create(ctx, 16);
  instance_buffer_upload(ctx, buffer, data.data(), count());
}

void instances::draw(context &ctx, mesh3d_kind mesh) const {
  if (buffer.id != 0 && count() > 0)
    draw_instanced3d(ctx, mesh, buffer, 0, count());
}

void instances::draw_range(context &ctx, mesh3d_kind mesh, u32 from, u32 to) const {
  to = std::min(to, count());
  if (buffer.id != 0 && to > from)
    draw_instanced3d(ctx, mesh, buffer, from, to - from);
}

void instances::destroy(context &ctx) {
  if (buffer.id != 0)
    instance_buffer_destroy(ctx, buffer);
  buffer = {};
  data.clear();
}

// --- mesh_builder ------------------------------------------------------------------------

void mesh_builder::tri(vec2 a, vec2 b, vec2 c, f32 y, rgba color) {
  // Seen from above, table (x, y) is 3D (x, z): counter-clockwise from above
  // is clockwise in table terms.
  if (cross(b - a, c - a) > 0.0f)
    std::swap(b, c);
  for (const vec2 p : {a, b, c}) {
    pos.push_back({p.x * unit3d, y, p.y * unit3d});
    col.push_back(color);
  }
}

void mesh_builder::quad(vec2 a, vec2 b, vec2 c, vec2 d, f32 y, rgba color) {
  tri(a, b, c, y, color);
  tri(a, c, d, y, color);
}

void mesh_builder::rect(const obb &box, f32 y, rgba color) {
  quad(box.corner(0), box.corner(1), box.corner(2), box.corner(3), y, color);
}

void mesh_builder::disc(vec2 c, f32 r, f32 y, rgba color, i32 sides) {
  for (i32 i = 0; i < sides; ++i) {
    const f32 a0 = static_cast<f32>(i) / static_cast<f32>(sides) * 360.0f;
    const f32 a1 = static_cast<f32>(i + 1) / static_cast<f32>(sides) * 360.0f;
    tri(c, c + from_angle(a0) * r, c + from_angle(a1) * r, y, color);
  }
}

void mesh_builder::line(vec2 a, vec2 b, f32 width, f32 y, rgba color) {
  const vec2 d = b - a;
  if (length_sq(d) < 1e-6f)
    return;
  const vec2 n = vec2{-d.y, d.x} * (width * 0.5f / length(d));
  quad(a + n, b + n, b - n, a - n, y, color);
}

void mesh_builder::band(const std::vector<vec2> &pts, f32 width, f32 y, rgba color) {
  if (pts.size() < 2)
    return;
  const bool closed = pts.size() > 2 && distance(pts.front(), pts.back()) < 0.5f;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    line(pts[i], pts[i + 1], width, y, color);
    if (i > 0 || closed)
      disc(pts[i], width * 0.5f, y, color, 12);
  }
}

void mesh_set::build(context &ctx, mesh_builder &b) {
  // Triangles sorted into square tiles by their middle, one model or more per
  // tile, so the engine leaves out the tiles the camera does not see.
  constexpr f32 tile = mesh_tile * unit3d;
  std::map<std::pair<i32, i32>, mesh_builder> tiles;
  for (size_t t = 0; t + 2 < b.pos.size(); t += 3) {
    const vec3 mid = (b.pos[t] + b.pos[t + 1] + b.pos[t + 2]) * (1.0f / 3.0f);
    mesh_builder &to = tiles[{static_cast<i32>(std::floor(mid.x / tile)), static_cast<i32>(std::floor(mid.z / tile))}];
    to.pos.insert(to.pos.end(), b.pos.begin() + static_cast<std::ptrdiff_t>(t),
                  b.pos.begin() + static_cast<std::ptrdiff_t>(t + 3));
    to.col.insert(to.col.end(), b.col.begin() + static_cast<std::ptrdiff_t>(t),
                  b.col.begin() + static_cast<std::ptrdiff_t>(t + 3));
  }
  constexpr size_t chunk = 60000; // a multiple of 3
  for (auto &[key, part] : tiles)
    for (size_t at = 0; at < part.pos.size(); at += chunk) {
      mesh3d_data md;
      md.positions = part.pos.data() + at;
      md.colors = part.col.data() + at;
      md.vertex_count = static_cast<u32>(std::min(chunk, part.pos.size() - at));
      const model_handle h = model_create(ctx, md);
      if (h.id != 0)
        models.push_back(h);
    }
  b.pos.clear();
  b.col.clear();
}

void mesh_set::draw(context &ctx) const {
  for (const model_handle h : models)
    draw_model(ctx, h, {});
}

void mesh_set::destroy(context &ctx) {
  for (const model_handle h : models)
    model_unload(ctx, h);
  models.clear();
}

// --- The whole view ----------------------------------------------------------------------

const char *overlay_name(overlay o) {
  static const char *names[] = {"Không", "Quận", "Khối", "Đi bộ", "Xe chạy"};
  return names[static_cast<i32>(o)];
}

void view_init(context &ctx) { cutaway_init(ctx); }

void view_shutdown(context &ctx) { cutaway_shutdown(ctx); }

void view_build(context &ctx, const city_map &map) {
  view_cleanup(ctx);
  chunk_grid(map);
  ground_build(ctx, map);
  buildings_build(ctx, map);
  props_build(ctx, map);
  debug_build(ctx, map);
}

void view_draw(context &ctx, const city_map &map, const view_options &opt) {
  view_cull_update(ctx, map, opt);
  ground_draw(ctx, map, opt);
  buildings_draw(ctx, opt);
  props_draw(ctx, opt);
  cutaway_draw(ctx, map, opt);
  debug_draw(ctx, map, opt);
}

void view_cleanup(context &ctx) {
  ground_cleanup(ctx);
  buildings_cleanup(ctx);
  props_cleanup(ctx);
  cutaway_cleanup(ctx);
  debug_cleanup(ctx);
}

} // namespace sandtable::city
