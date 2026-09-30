#include "render_common.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// Looking into the generator's work: pins on businesses, gang seats and open
// places, the road graph, names over the map, and a card telling what is
// under the mouse.

namespace sandtable::city {

namespace {

instances pins;  // businesses and open places
instances seats; // where gangs could set up

rgba spot_color(spot_kind k) {
  static const rgba c[] = {rgb8(170, 140, 90),  rgb8(120, 120, 130), rgb8(80, 180, 80), rgb8(230, 170, 60),
                           rgb8(90, 140, 200),  rgb8(60, 150, 60),   rgb8(60, 120, 170), rgb8(240, 240, 240),
                           rgb8(200, 60, 200),  rgb8(250, 250, 120)};
  return c[static_cast<i32>(k)];
}

void text(context &ctx, const char *str, vec2 pos, f32 size, rgba col, font_handle font) {
  const vec2 p{std::floor(pos.x), std::floor(pos.y)};
  draw_text(ctx, str, p + vec2{1.0f, 1.0f}, size, {0.05f, 0.04f, 0.03f, 0.85f * col.a}, font);
  draw_text(ctx, str, p, size, col, font);
}

void centred(context &ctx, const char *str, vec2 pos, f32 size, rgba col, font_handle font) {
  const vec2 w = text_measure(ctx, str, size, font);
  text(ctx, str, pos - vec2{w.x * 0.5f, 0.0f}, size, col, font);
}

// District names over the map when it is seen whole.
void district_labels(context &ctx, const city_map &map, const view_options &opt, font_handle font) {
  if (state.cam_distance < 30.0f && opt.layer != overlay::districts)
    return;
  for (i32 i = 0; i < static_cast<i32>(map.districts.size()); ++i) {
    const district &d = map.districts[static_cast<size_t>(i)];
    // The one under the mouse has its own, larger label (render_hover.cpp).
    if (d.cells == 0 || i == opt.hover_district)
      continue;
    bool visible = false;
    const vec2 s = table_to_screen(ctx, d.centroid, 0.5f, &visible);
    if (!visible)
      continue;
    const f32 k = ui_scale(ctx);
    centred(ctx, d.name.c_str(), s, 24.0f * k, {1.0f, 0.96f, 0.86f, 1.0f}, font);
    centred(ctx, district_name(d.kind), s + vec2{0.0f, 25.0f * k}, 16.0f * k, {0.85f, 0.85f, 0.8f, 0.9f}, font);
  }
}

// Shop names up close, the nearest to the middle of the screen first.
void business_labels(context &ctx, const city_map &map, font_handle font) {
  if (state.cam_distance > 16.0f)
    return;
  const vec2 mid = screen_size(ctx) * 0.5f;
  struct item {
    f32 d;
    vec2 at;
    i32 id;
  };
  std::vector<item> shown;
  for (i32 i = 0; i < static_cast<i32>(map.businesses.size()); ++i) {
    const business &bz = map.businesses[static_cast<size_t>(i)];
    const building &bd = map.buildings[static_cast<size_t>(bz.building)];
    bool visible = false;
    const vec2 s = table_to_screen(ctx, bd.box.center, (bd.height + 6.0f) * unit3d, &visible);
    // Clear of the HUD rows at the top and the bottom.
    if (visible && s.x > 0.0f && s.y > 64.0f && s.x < mid.x * 2.0f && s.y < mid.y * 2.0f - 56.0f)
      shown.push_back({distance(s, mid), s, i});
  }
  std::sort(shown.begin(), shown.end(), [](const item &a, const item &b) { return a.d < b.d; });
  if (shown.size() > 24)
    shown.resize(24);
  for (const item &it : shown) {
    const business &bz = map.businesses[static_cast<size_t>(it.id)];
    rgba c = business_color(bz.kind);
    c = lerp(c, rgba{1, 1, 1, 1}, 0.45f);
    centred(ctx, bz.name.c_str(), it.at, 17.0f * ui_scale(ctx), c, font);
  }
}

// What is under the mouse, as a card in the corner.
void hover_card(context &ctx, const city_map &map, font_handle font) {
  vec2 at;
  if (!mouse_on_table(ctx, &at))
    return;
  std::vector<std::string> lines;
  char buf[256];
  const cell_info *c = map.cell_at(at);
  if (!c)
    return;
  const district &d = map.districts[c->district];
  std::snprintf(buf, sizeof(buf), "%s  (%s)", d.name.c_str(), district_name(d.kind));
  lines.emplace_back(buf);
  if (c->block >= 0) {
    const block &bk = map.blocks[static_cast<size_t>(c->block)];
    std::snprintf(buf, sizeof(buf), "Khối %d: %d nhà, %d cơ sở, giáp %d khối", c->block,
                  static_cast<i32>(bk.buildings.size()), static_cast<i32>(bk.businesses.size()),
                  static_cast<i32>(bk.neighbours.size()));
    lines.emplace_back(buf);
  }
  const i32 rid = map.road_at(at);
  if (rid >= 0 && !map.roads[static_cast<size_t>(rid)].name.empty())
    lines.push_back(map.roads[static_cast<size_t>(rid)].name);
  const i32 bi = view_pick(ctx, map, mouse_pos(ctx));
  if (bi >= 0) {
    const building &b = map.buildings[static_cast<size_t>(bi)];
    if (b.road >= 0 && b.number > 0)
      std::snprintf(buf, sizeof(buf), "%s %d tầng, số %d %s", building_name(b.kind), b.floors, b.number,
                    map.roads[static_cast<size_t>(b.road)].name.c_str());
    else
      std::snprintf(buf, sizeof(buf), "%s %d tầng%s", building_name(b.kind), b.floors, b.door_ok ? "" : " (kín lối)");
    lines.emplace_back(buf);
    if (b.business >= 0) {
      const business &bz = map.businesses[static_cast<size_t>(b.business)];
      std::snprintf(buf, sizeof(buf), "%s  -  %s, hạng %d", bz.name.c_str(), business_name(bz.kind), bz.tier);
      lines.emplace_back(buf);
      std::snprintf(buf, sizeof(buf), "Thu %d nghìn/ngày, bảo kê %d nghìn/tuần", bz.income, bz.protection);
      lines.emplace_back(buf);
    }
    if (std::find(map.hq_sites.begin(), map.hq_sites.end(), bi) != map.hq_sites.end())
      lines.emplace_back("Có thể đặt trụ sở băng");
  }
  for (const spot &s : map.spots)
    if (s.kind != spot_kind::park && s.kind != spot_kind::roundabout && s.box.contains(at)) {
      lines.push_back(std::string("Chỗ: ") + spot_name(s.kind));
      break;
    }
  const f32 k = ui_scale(ctx);
  const f32 size = 20.0f * k, line_h = 24.0f * k;
  f32 w = 0.0f;
  for (const std::string &l : lines)
    w = std::max(w, text_measure(ctx, l.c_str(), size, font).x);
  const vec2 scr = screen_size(ctx);
  const vec2 pos{scr.x - w - 24.0f * k, scr.y - static_cast<f32>(lines.size()) * line_h - 110.0f * k};
  draw_rect(ctx, {pos - vec2{10.0f, 8.0f} * k, {w + 20.0f * k, static_cast<f32>(lines.size()) * line_h + 16.0f * k}},
            {0.1f, 0.12f, 0.11f, 0.85f});
  for (size_t i = 0; i < lines.size(); ++i)
    text(ctx, lines[i].c_str(), pos + vec2{0.0f, static_cast<f32>(i) * line_h}, size,
         i == 0 ? rgba{1.0f, 0.9f, 0.7f, 1.0f} : rgba{0.92f, 0.92f, 0.88f, 1.0f}, font);
}

} // namespace

void debug_build(context &ctx, const city_map &map) {
  pins.clear();
  seats.clear();
  for (const business &bz : map.businesses) {
    const building &b = map.buildings[static_cast<size_t>(bz.building)];
    pins.post(b.box.center, b.height + 4.0f, 1.2f + 0.6f * static_cast<f32>(bz.tier), 10.0f, business_color(bz.kind));
  }
  for (const spot &s : map.spots)
    if (s.kind != spot_kind::park)
      pins.post(s.box.center, 0.0f, 3.0f, 3.0f, spot_color(s.kind));
  for (const i32 h : map.hq_sites) {
    const building &b = map.buildings[static_cast<size_t>(h)];
    seats.post(b.box.center, b.height, 3.0f, 60.0f, rgb8(230, 40, 30));
  }
  pins.upload(ctx);
  seats.upload(ctx);
}

void debug_draw(context &ctx, const city_map &map, const view_options &opt) {
  if (opt.markers) {
    material3d_set(ctx, {.unlit = true, .cast_shadows = false});
    pins.draw(ctx, mesh3d_cylinder_low);
    seats.draw(ctx, mesh3d_cylinder_low);
    material3d_set(ctx, {});
  }
  if (opt.graph) {
    const f32 lift = 0.12f;
    for (const road_edge &e : map.edges) {
      const road_kind k = map.roads[static_cast<size_t>(e.road)].kind;
      const rgba c = k == road_kind::avenue ? colors::yellow : k == road_kind::alley ? rgba{0.6f, 0.6f, 0.6f, 1.0f}
                                                                                     : colors::white;
      for (size_t i = 0; i + 1 < e.pts.size(); ++i)
        gizmo_line3d(ctx, to3d(e.pts[i], lift), to3d(e.pts[i + 1], lift), c);
    }
    for (const road_node &n : map.nodes)
      gizmo_sphere3d(ctx, to3d(n.pos, lift), n.edges.size() >= 3 ? 0.25f : 0.12f,
                     n.roundabout ? rgba{1.0f, 0.3f, 1.0f, 1.0f} : n.edges.size() == 1 ? colors::red : colors::green);
  }
}

void view_draw_ui(context &ctx, const city_map &map, const view_options &opt, font_handle font) {
  if (opt.labels) {
    district_labels(ctx, map, opt, font);
    business_labels(ctx, map, font);
  }
  hover_label(ctx, map, opt, font);
  if (opt.debug_card)
    hover_card(ctx, map, font);
}

void debug_cleanup(context &ctx) {
  pins.destroy(ctx);
  seats.destroy(ctx);
}

} // namespace sandtable::city
