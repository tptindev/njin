#include "world.h"
#include "crowd.h"
#include "gang.h"
#include "physics.h"
#include "view.h"
#include "weather.h"

#include <algorithm>
#include <cmath>

#include <chrono>
#include <string>

namespace sandtable {

namespace {

city::city_map map;
city::view_options view;
bool cut_around = false; // C: cut open everything near the middle of the view

// The camera before a building was focused, to go back to.
struct saved_camera {
  bool on = false;
  vec2 target{};
  f32 distance = 0.0f;
  f32 yaw = 0.0f;
};
saved_camera before_focus;

// `to` in degrees, turned by whole turns to lie within half a turn of `from`,
// so the camera takes the short way round.
f32 nearest_turn(f32 from, f32 to) {
  return to + 360.0f * std::round((from - to) / 360.0f);
}

} // namespace

const city::city_map &world() { return map; }
city::view_options &world_view() { return view; }

void world_focus(i32 id) {
  if (id < 0 || id >= static_cast<i32>(map.buildings.size()))
    return;
  if (!before_focus.on)
    before_focus = {true, state.cam_target_goal, state.cam_distance_goal, state.cam_yaw_goal};
  if (id != view.selected)
    view.floor = 0;
  view.selected = id;
  const city::building &b = map.buildings[static_cast<size_t>(id)];
  // From its front, across the street, and near enough that it and the
  // pavement before it fill the middle of the picture (fovy 40).
  const vec2 front = b.front();
  state.cam_yaw_goal = nearest_turn(state.cam_yaw, std::atan2(front.x, front.y) * 180.0f / pi);
  const f32 reach = (length(b.box.half) + 40.0f) * unit3d;
  view_focus(b.box.center, reach / std::tan(20.0f * pi / 180.0f));
}

void world_unfocus() {
  view.selected = -1;
  view.floor = 0;
  if (before_focus.on) {
    view_focus(before_focus.target, before_focus.distance);
    state.cam_yaw_goal = nearest_turn(state.cam_yaw, before_focus.yaw);
    before_focus.on = false;
  }
}

void world_generate(context &ctx, u32 seed) {
  state.seed = seed;
  city::city_desc desc;
  desc.seed = seed;
  desc.width = world_width;
  desc.height = world_height;
  city::generate(map, desc);
  view.selected = -1;
  view.floor = 0;
  view.cut.clear();
  before_focus.on = false;
  NJIN_INFO("city %u: %d roads, %d blocks, %d buildings, %d businesses, %d places, %.0f ms, %s", seed,
           static_cast<i32>(map.roads.size()), static_cast<i32>(map.blocks.size()),
           static_cast<i32>(map.buildings.size()), static_cast<i32>(map.businesses.size()),
           static_cast<i32>(map.spots.size()), static_cast<f64>(map.report.gen_ms),
           map.report.ok() ? "ok" : "FAILED");
  for (const std::string &e : map.report.errors)
    NJIN_WARN("city %u: %s", seed, e.c_str());
  city::view_build(ctx, map);
  physics_build(ctx, map);
  crowd_spawn(ctx, seed);
  gang_start(ctx, seed);
}

void world_input(context &ctx) {
  // A popup has the keys and the mouse.
  // The gangs' turf, for the map (set each frame: a reset view keeps it).
  view.turf = &turf_owner();
  if (view.turf_colours.size() != gangs().size()) {
    view.turf_colours.clear();
    for (const gang_state &g : gangs())
      view.turf_colours.push_back(g.colour);
  }
  view.turf_version = turf_version();
  if (state.popup_open) {
    view.hover_district = view.hover_building = -1;
    return;
  }
  if (key_pressed(ctx, key_n))
    world_generate(ctx, state.seed + 1);
  if (key_pressed(ctx, key_b) && state.seed > 1)
    world_generate(ctx, state.seed - 1);
  if (key_pressed(ctx, key_r)) {
    const u64 t = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
    world_generate(ctx, static_cast<u32>((t ^ (t >> 32)) % 100000u) + 1u);
  }
  if (key_pressed(ctx, key_f1))
    view.layer = static_cast<city::overlay>((static_cast<i32>(view.layer) + 1) % static_cast<i32>(city::overlay::count));
  if (key_pressed(ctx, key_f2))
    view.labels = !view.labels;
  if (key_pressed(ctx, key_f3))
    view.graph = !view.graph;
  if (key_pressed(ctx, key_f4))
    view.markers = !view.markers;
  if (key_pressed(ctx, key_c))
    cut_around = !cut_around;
  if (key_pressed(ctx, key_t))
    state.hour = darkness() < 0.5f ? 21.0f : 12.0f; // toggle night/day
  if (key_pressed(ctx, key_escape) && view.selected >= 0)
    world_unfocus();
  // What the mouse is over, to light up.
  view.hover_district = view.hover_building = -1;
  vec2 under;
  if (!ui_mouse_over(ctx)) {
    if (mouse_on_table(ctx, &under))
      if (const city::cell_info *c = map.cell_at(under))
        view.hover_district = c->district;
    view.hover_building = city::view_pick(ctx, map, mouse_pos(ctx));
  }
  // A building focuses it; a click beside the one in focus lets it go.
  if (mouse_pressed(ctx, mouse_left) && !ui_mouse_over(ctx)) {
    const i32 hit = view.hover_building;
    if (hit >= 0)
      world_focus(hit);
    else if (view.selected >= 0)
      world_unfocus();
  }
  // Up and down the floors of the open building (or of the tallest one open
  // round the middle of the view).
  i32 top = 0;
  if (view.selected >= 0)
    top = map.buildings[static_cast<size_t>(view.selected)].floors - 1;
  else if (cut_around)
    for (const i32 id : view.cut)
      top = std::max(top, map.buildings[static_cast<size_t>(id)].floors - 1);
  if (key_pressed(ctx, key_page_up) || key_pressed(ctx, key_right_bracket))
    ++view.floor;
  if (key_pressed(ctx, key_page_down) || key_pressed(ctx, key_left_bracket))
    --view.floor;
  view.floor = std::clamp(view.floor, 0, std::max(0, top));
}

void world_cut_around(bool on) { cut_around = on; }
bool world_cut_around_on() { return cut_around; }

void world_update_view() {
  view.cut.clear();
  // One building in focus is the only one open: the camera looks steeply
  // down at it from its front instead, so the houses round it do not hide it.
  view.around = cut_around && view.selected < 0;
  if (view.selected >= 0)
    view.cut.push_back(view.selected);
  // Close in, the blocks round the middle of the view; from afar it would be
  // the whole town with its roofs off.
  if (view.around && state.cam_distance < 30.0f) {
    const f32 r = 40.0f + state.cam_distance * 5.0f;
    for (i32 i = 0; i < static_cast<i32>(map.buildings.size()); ++i)
      if (distance(map.buildings[static_cast<size_t>(i)].box.center, state.cam_target) < r)
        view.cut.push_back(i);
  }
  // A building picked is in focus: sharp in a circle round it and the
  // pavement before it, blurred beyond; full detail a little further out.
  view.focused = view.selected >= 0;
  state.cam_steep_goal = view.focused ? 1.0f : 0.0f;
  if (view.focused) {
    const city::obb &sel = map.buildings[static_cast<size_t>(view.selected)].box;
    view.focus = sel.center;
    view.sharp_radius = length(sel.half) + 30.0f;
    view.focus_radius = view.sharp_radius + 160.0f;
  }
  // Look up at the open floor, not at the street under it.
  state.cam_lift_goal = view.selected >= 0 || (view.around && !view.cut.empty())
                            ? static_cast<f32>(view.floor) * city::floor_height * unit3d
                            : 0.0f;
  std::sort(view.cut.begin(), view.cut.end());
  view.cut.erase(std::unique(view.cut.begin(), view.cut.end()), view.cut.end());
}

} // namespace sandtable
