#include "render_common.h"

#include <cmath>
#include <algorithm>
#include <functional>
#include <utility>

// What the mouse is over, lit up: the district (a light tint over its ground,
// a bright line round its edge, its name large on the map) and the building
// (a bright outline at its roof). And the gangs' turf, when asked for: each gang's
// blocks tinted and outlined in its colour.

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

// A patch of the table: its ground tinted, and its edge, thin and wide.
struct region {
  mesh_set fill, edge, edge_far;
  void destroy(context &ctx) {
    fill.destroy(ctx);
    edge.destroy(ctx);
    edge_far.destroy(ctx);
  }
  void draw(context &ctx) {
    fill.draw(ctx);
    (state.cam_distance > far_distance ? edge_far : edge).draw(ctx);
  }
};
std::vector<region> districts;
// The gangs' turf, one region each, and the version of it that was built.
std::vector<region> turf;
u32 turf_built = ~0u;
// The frame round the building under the mouse, and which one it is.
instances frame;
i32 framed = -1;
// The ring under the man looked at: rebuilt every frame, as he walks.
instances person_ring;
constexpr f32 ring_radius = 6.0f;    // world units, about a metre round him
constexpr f32 ring_width = 1.2f;
constexpr i32 ring_segments = 20;
const rgba ring_col{1.0f, 0.95f, 0.7f, 1.0f};

// draw_model uses its own materials, not material3d_set(). Keep overlay alpha
// in the material so the renderer sends it to the translucent, no-depth-write
// pass, and never lets hover/turf geometry enter the sun's shadow map.
void build_overlay(context &ctx, mesh_builder &builder, mesh_set &out, f32 alpha) {
  for (rgba &c : builder.col)
    c.a = 1.0f;
  out.build(ctx, builder);
  for (const model_handle h : out.models)
    for (i32 i = 0; i < model_material_count(ctx, h); ++i) {
      model_material mm = model_material_get(ctx, h, i);
      mm.color.a = alpha;
      mm.surface.unlit = true;
      mm.surface.cast_shadows = false;
      model_material_set(ctx, h, i, mm);
    }
}

// The ring, `ring_segments` short tangential bars, centred on `at`, at the
// height people's feet are drawn (draw_person's lift, 0.04 3D units: the
// street surface), `lift` world units higher for a floor inside.
void build_ring(context &ctx, vec2 at, f32 lift) {
  person_ring.clear();
  const f32 base = 0.04f / unit3d + lift;
  for (i32 i = 0; i < ring_segments; ++i) {
    const f32 a0 = 360.0f * static_cast<f32>(i) / static_cast<f32>(ring_segments);
    const f32 a1 = 360.0f * static_cast<f32>(i + 1) / static_cast<f32>(ring_segments);
    const vec2 p0 = at + from_angle(a0) * ring_radius, p1 = at + from_angle(a1) * ring_radius;
    person_ring.box((p0 + p1) * 0.5f, base, {distance(p0, p1) * 1.2f, 0.3f, ring_width}, angle_of(p1 - p0),
                    ring_col);
  }
  person_ring.upload(ctx);
}

// The cells `in` says yes to, as runs along each row, and the edges between
// them and the rest (cell sides, merged where they run on).
void build_region(context &ctx, const city_map &map, const std::function<bool(i32, i32)> &inside, rgba tint,
                  rgba edge, region &out) {
  const f32 cs = map.desc.cell;
  const auto in = [&](i32 x, i32 y) { return x >= 0 && y >= 0 && x < map.cols && y < map.rows && inside(x, y); };
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
  build_overlay(ctx, f, out.fill, tint.a);
  build_overlay(ctx, l, out.edge, edge.a);
  build_overlay(ctx, lf, out.edge_far, edge.a);
}

// Only the roof perimeter: no ground-level outline or vertical corner posts.
void build_frame(context &ctx, const building &b) {
  frame.clear();
  const f32 top = b.height + 1.0f;
  for (i32 k = 0; k < 4; ++k) {
    const vec2 a = b.box.corner(k), c = b.box.corner((k + 1) % 4);
    const vec2 d = c - a;
    frame.box((a + c) * 0.5f, top - frame_width * 0.5f,
              {length(d) + frame_width, frame_width, frame_width}, angle_of(d), frame_col);
  }
  frame.upload(ctx);
}

} // namespace

void hover_build(context &ctx, const city_map &map) {
  hover_cleanup(ctx);
  districts.resize(map.districts.size());
  for (i32 d = 0; d < static_cast<i32>(map.districts.size()); ++d)
    if (map.districts[static_cast<size_t>(d)].cells > 0)
      build_region(
          ctx, map, [&](i32 x, i32 y) { return map.at(x, y).district == d; }, tint, edge,
          districts[static_cast<size_t>(d)]);
}

void hover_draw(context &ctx, const city_map &map, const view_options &opt) {
  material3d_set(ctx, {.unlit = true, .cast_shadows = false});
  // The gangs' turf, rebuilt when it changes.
  if (opt.show_turf && opt.turf != nullptr) {
    if (turf_built != opt.turf_version) {
      for (region &r : turf)
        r.destroy(ctx);
      turf.assign(opt.turf_colours.size(), {});
      const std::vector<i8> &owner = *opt.turf;
      for (i32 g = 0; g < static_cast<i32>(turf.size()); ++g) {
        const rgba c = opt.turf_colours[static_cast<size_t>(g)];
        build_region(
            ctx, map,
            [&](i32 x, i32 y) {
              const i32 bk = map.at(x, y).block;
              return bk >= 0 && static_cast<size_t>(bk) < owner.size() && owner[static_cast<size_t>(bk)] == g;
            },
            {c.r, c.g, c.b, 0.26f}, {std::min(1.0f, c.r * 1.2f), std::min(1.0f, c.g * 1.2f), std::min(1.0f, c.b * 1.2f), 1.0f},
            turf[static_cast<size_t>(g)]);
      }
      turf_built = opt.turf_version;
    }
    for (region &r : turf)
      r.draw(ctx);
  }
  if (opt.hover_district >= 0 && opt.hover_district < static_cast<i32>(districts.size()))
    districts[static_cast<size_t>(opt.hover_district)].draw(ctx);
  // The building in focus is outlined by the cutaway already.
  const i32 b = opt.hover_building != opt.selected ? opt.hover_building : -1;
  if (b >= 0) {
    if (b != framed) {
      build_frame(ctx, map.buildings[static_cast<size_t>(b)]);
      framed = b;
    }
    frame.draw(ctx, mesh3d_cube);
  }
  if (opt.person_focused) {
    build_ring(ctx, opt.person_focus, opt.person_lift);
    person_ring.draw(ctx, mesh3d_cube);
  }
  material3d_set(ctx, {});
}

void hover_label(context &ctx, const city_map &map, const view_options &opt, font_handle font) {
  if (opt.hover_district < 0 || opt.hover_district >= static_cast<i32>(map.districts.size()))
    return;
  // Close in, one is inside the district: its name would only be in the way.
  if (state.cam_distance < 30.0f)
    return;
  const district &d = map.districts[static_cast<size_t>(opt.hover_district)];
  bool visible = false;
  vec2 at = table_to_screen(ctx, d.centroid, 0.5f, &visible);
  const vec2 scr = screen_size(ctx);
  const f32 k = ui_scale(ctx);
  // Its middle out of the picture: over the mouse instead.
  if (!visible || at.x < 80.0f * k || at.y < 120.0f * k || at.x > scr.x - 80.0f * k || at.y > scr.y - 140.0f * k)
    at = mouse_pos(ctx) - vec2{0.0f, 80.0f * k};
  // Clear of the HUD at the top and the dock at the bottom.
  at.y = clamp(at.y, 90.0f * k, scr.y - 170.0f * k);
  const f32 big = 34.0f * k, small = 18.0f * k;
  const vec2 w = text_measure(ctx, d.name.c_str(), big, font);
  const vec2 w2 = text_measure(ctx, district_name(d.kind), small, font);
  const vec2 p{std::floor(at.x - w.x * 0.5f), std::floor(at.y)};
  const f32 box_w = std::max(w.x, w2.x) + 32.0f * k;
  draw_rect(ctx, {{std::floor(at.x - box_w * 0.5f), p.y - 8.0f * k}, {box_w, big + small + 22.0f * k}},
            {0.08f, 0.07f, 0.05f, 0.62f});
  draw_text(ctx, d.name.c_str(), p + vec2{2.0f, 2.0f}, big, {0.05f, 0.04f, 0.03f, 0.9f}, font);
  draw_text(ctx, d.name.c_str(), p, big, edge, font);
  draw_text(ctx, district_name(d.kind), {std::floor(at.x - w2.x * 0.5f), p.y + big + 3.0f * k}, small,
            {0.95f, 0.92f, 0.84f, 1.0f}, font);
}

void hover_cleanup(context &ctx) {
  for (region &r : districts)
    r.destroy(ctx);
  districts.clear();
  for (region &r : turf)
    r.destroy(ctx);
  turf.clear();
  turf_built = ~0u;
  frame.destroy(ctx);
  framed = -1;
  person_ring.destroy(ctx);
}

} // namespace sandtable::city
