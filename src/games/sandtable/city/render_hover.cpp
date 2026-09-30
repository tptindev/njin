#include "render_common.h"

#include <cmath>
#include <utility>

// What the mouse is over, lit up: the district (a light tint over its ground,
// a bright line round its edge, its name large on the map) and the building
// (a bright frame round it).

namespace sandtable::city {

namespace {

const rgba tint{1.0f, 0.88f, 0.5f, 0.2f};
const rgba edge{1.0f, 0.86f, 0.45f, 1.0f};
const rgba frame_col{1.0f, 0.95f, 0.7f, 1.0f};
constexpr f32 layer_tint = 0.07f;  // over roads, markings and islands
constexpr f32 layer_edge = 0.08f;
// World units: the thin line close up, the wide one from afar (still a few
// pixels across when the whole table is in view).
constexpr f32 edge_width = 3.0f;
constexpr f32 edge_width_far = 10.0f;
constexpr f32 far_distance = 40.0f; // camera distance, 3D units
constexpr f32 frame_width = 1.2f;  // world units

// Per district: its ground tinted, and its edge, thin and wide.
std::vector<mesh_set> fills, edges, edges_far;
// The frame round the building under the mouse, and which one it is.
instances frame;
i32 framed = -1;

// The cells of district `d`, as runs along each row, and the edges between
// them and the rest (cell sides, merged where they run on).
void build_district(context &ctx, const city_map &map, i32 d, mesh_set &fill, mesh_set &line, mesh_set &line_far) {
  const f32 cs = map.desc.cell;
  const auto in = [&](i32 x, i32 y) {
    return x >= 0 && y >= 0 && x < map.cols && y < map.rows && map.at(x, y).district == d;
  };
  mesh_builder f, l, lf;
  for (i32 y = 0; y < map.rows; ++y)
    for (i32 x = 0; x < map.cols;) {
      if (!in(x, y)) {
        ++x;
        continue;
      }
      i32 end = x;
      while (end < map.cols && in(end, y))
        ++end;
      const f32 x0 = static_cast<f32>(x) * cs, x1 = static_cast<f32>(end) * cs;
      const f32 y0 = static_cast<f32>(y) * cs, y1 = y0 + cs;
      f.quad({x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, layer_tint, tint);
      x = end;
    }
  // Sides between a row of cells and the next one down.
  for (i32 y = 0; y <= map.rows; ++y)
    for (i32 x = 0; x < map.cols;) {
      if (in(x, y - 1) == in(x, y)) {
        ++x;
        continue;
      }
      i32 end = x;
      while (end < map.cols && in(end, y - 1) != in(end, y) && in(end, y) == in(x, y))
        ++end;
      for (auto [b, w] : {std::pair{&l, edge_width}, std::pair{&lf, edge_width_far}})
        b->line({static_cast<f32>(x) * cs, static_cast<f32>(y) * cs},
                {static_cast<f32>(end) * cs, static_cast<f32>(y) * cs}, w, layer_edge, edge);
      x = end;
    }
  // Sides between a column of cells and the next one along.
  for (i32 x = 0; x <= map.cols; ++x)
    for (i32 y = 0; y < map.rows;) {
      if (in(x - 1, y) == in(x, y)) {
        ++y;
        continue;
      }
      i32 end = y;
      while (end < map.rows && in(x - 1, end) != in(x, end) && in(x, end) == in(x, y))
        ++end;
      for (auto [b, w] : {std::pair{&l, edge_width}, std::pair{&lf, edge_width_far}})
        b->line({static_cast<f32>(x) * cs, static_cast<f32>(y) * cs},
                {static_cast<f32>(x) * cs, static_cast<f32>(end) * cs}, w, layer_edge, edge);
      y = end;
    }
  fill.build(ctx, f);
  line.build(ctx, l);
  line_far.build(ctx, lf);
}

// Twelve thin bars along the edges of building `b`'s box.
void build_frame(context &ctx, const building &b) {
  frame.clear();
  const f32 top = b.height + 1.0f;
  for (i32 k = 0; k < 4; ++k) {
    const vec2 a = b.box.corner(k), c = b.box.corner((k + 1) % 4);
    const vec2 d = c - a;
    for (const f32 h : {0.3f, top})
      frame.box((a + c) * 0.5f, h - frame_width * 0.5f, {length(d) + frame_width, frame_width, frame_width},
                angle_of(d), frame_col);
    frame.post(a, 0.0f, frame_width * 0.5f, top, frame_col);
  }
  frame.upload(ctx);
}

} // namespace

void hover_build(context &ctx, const city_map &map) {
  hover_cleanup(ctx);
  fills.resize(map.districts.size());
  edges.resize(map.districts.size());
  edges_far.resize(map.districts.size());
  for (i32 d = 0; d < static_cast<i32>(map.districts.size()); ++d)
    if (map.districts[static_cast<size_t>(d)].cells > 0)
      build_district(ctx, map, d, fills[static_cast<size_t>(d)], edges[static_cast<size_t>(d)],
                     edges_far[static_cast<size_t>(d)]);
}

void hover_draw(context &ctx, const city_map &map, const view_options &opt) {
  material3d_set(ctx, {.unlit = true, .cast_shadows = false});
  if (opt.hover_district >= 0 && opt.hover_district < static_cast<i32>(fills.size())) {
    fills[static_cast<size_t>(opt.hover_district)].draw(ctx);
    (state.cam_distance > far_distance ? edges_far : edges)[static_cast<size_t>(opt.hover_district)].draw(ctx);
  }
  // The building in focus is outlined by the cutaway already.
  const i32 b = opt.hover_building != opt.selected ? opt.hover_building : -1;
  if (b >= 0) {
    if (b != framed) {
      build_frame(ctx, map.buildings[static_cast<size_t>(b)]);
      framed = b;
    }
    frame.draw(ctx, mesh3d_cube);
  }
  material3d_set(ctx, {});
}

void hover_label(context &ctx, const city_map &map, const view_options &opt, font_handle font) {
  if (opt.hover_district < 0 || opt.hover_district >= static_cast<i32>(map.districts.size()))
    return;
  const district &d = map.districts[static_cast<size_t>(opt.hover_district)];
  bool visible = false;
  vec2 at = table_to_screen(ctx, d.centroid, 0.5f, &visible);
  const vec2 scr = screen_size(ctx);
  // Its middle out of the picture: over the mouse instead.
  if (!visible || at.x < 40.0f || at.y < 80.0f || at.x > scr.x - 40.0f || at.y > scr.y - 70.0f)
    at = mouse_pos(ctx) - vec2{0.0f, 44.0f};
  // Clear of the key help at the bottom and the rows at the top.
  at.y = clamp(at.y, 80.0f, scr.y - 64.0f - 46.0f);
  const f32 big = 22.0f, small = 12.0f;
  const vec2 w = text_measure(ctx, d.name.c_str(), big, font);
  const vec2 w2 = text_measure(ctx, district_name(d.kind), small, font);
  const vec2 p{std::floor(at.x - w.x * 0.5f), std::floor(at.y)};
  const f32 box_w = std::max(w.x, w2.x) + 20.0f;
  draw_rect(ctx, {{std::floor(at.x - box_w * 0.5f), p.y - 5.0f}, {box_w, big + small + 14.0f}},
            {0.08f, 0.07f, 0.05f, 0.62f});
  draw_text(ctx, d.name.c_str(), p + vec2{1.0f, 1.0f}, big, {0.05f, 0.04f, 0.03f, 0.9f}, font);
  draw_text(ctx, d.name.c_str(), p, big, edge, font);
  draw_text(ctx, district_name(d.kind), {std::floor(at.x - w2.x * 0.5f), p.y + big + 2.0f}, small,
            {0.95f, 0.92f, 0.84f, 1.0f}, font);
}

void hover_cleanup(context &ctx) {
  for (mesh_set &m : fills)
    m.destroy(ctx);
  for (mesh_set &m : edges)
    m.destroy(ctx);
  for (mesh_set &m : edges_far)
    m.destroy(ctx);
  edges_far.clear();
  fills.clear();
  edges.clear();
  frame.destroy(ctx);
  framed = -1;
}

} // namespace sandtable::city
