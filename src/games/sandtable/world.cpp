#include "world.h"
#include "crowd.h"
#include "view.h"

#include <algorithm>
#include <cmath>

#include <chrono>
#include <string>

namespace sandtable {

namespace {

city::city_map map;
city::view_options view;
bool cut_around = false; // C: cut open everything near the middle of the view

} // namespace

const city::city_map &world() { return map; }
city::view_options &world_view() { return view; }

void world_generate(context &ctx, u32 seed) {
  state.seed = seed;
  city::city_desc desc;
  desc.seed = seed;
  desc.width = world_width;
  desc.height = world_height;
  city::generate(map, desc);
  view.selected = -1;
  view.cut.clear();
  NJIN_INFO("city %u: %d roads, %d blocks, %d buildings, %d businesses, %d places, %.0f ms, %s", seed,
           static_cast<i32>(map.roads.size()), static_cast<i32>(map.blocks.size()),
           static_cast<i32>(map.buildings.size()), static_cast<i32>(map.businesses.size()),
           static_cast<i32>(map.spots.size()), static_cast<f64>(map.report.gen_ms),
           map.report.ok() ? "ok" : "FAILED");
  for (const std::string &e : map.report.errors)
    NJIN_WARN("city %u: %s", seed, e.c_str());
  city::view_build(ctx, map);
  crowd_spawn(seed);
}

void world_input(context &ctx) {
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
  if (key_pressed(ctx, key_escape))
    view.selected = -1;
  if (mouse_pressed(ctx, mouse_left) && !ui_mouse_over(ctx)) {
    const i32 was = view.selected;
    view.selected = city::view_pick(ctx, map, mouse_pos(ctx));
    if (view.selected != was)
      view.floor = 0;
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

void world_update_view() {
  view.cut.clear();
  view.around = cut_around;
  if (view.selected >= 0) {
    view.cut.push_back(view.selected);
    // Anything close by between it and the camera is opened too, or a tall
    // neighbour would hide the inside we are looking at.
    const vec2 at = map.buildings[static_cast<size_t>(view.selected)].box.center;
    const camera3d cam = table_camera();
    const vec2 to_cam = normalize(vec2{cam.position.x / unit3d, cam.position.z / unit3d} - at);
    const city::obb &sel = map.buildings[static_cast<size_t>(view.selected)].box;
    // How far a box reaches out along `n`.
    const auto reach = [](const city::obb &o, vec2 n) {
      return std::fabs(dot(n, o.axis_x())) * o.half.x + std::fabs(dot(n, o.axis_y())) * o.half.y;
    };
    for (i32 i = 0; i < static_cast<i32>(map.buildings.size()); ++i) {
      const city::obb &nb = map.buildings[static_cast<size_t>(i)].box;
      const vec2 d = nb.center - at;
      const f32 len = length(d);
      if (len < 1.0f || dot(d, to_cam) <= 0.0f)
        continue;
      // The gap between the two, along the line between their middles: deep
      // tube houses back to back have their middles far apart but touch.
      const vec2 n = d / len;
      if (len - reach(nb, n) - reach(sel, n) < 40.0f)
        view.cut.push_back(i);
    }
  }
  // Close in, the blocks round the middle of the view; from afar it would be
  // the whole town with its roofs off.
  if (cut_around && state.cam_distance < 30.0f) {
    const f32 r = 40.0f + state.cam_distance * 5.0f;
    for (i32 i = 0; i < static_cast<i32>(map.buildings.size()); ++i)
      if (distance(map.buildings[static_cast<size_t>(i)].box.center, state.cam_target) < r)
        view.cut.push_back(i);
  }
  // A building picked is in focus: full detail round it, the town beyond hazy.
  view.focused = view.selected >= 0;
  if (view.focused) {
    const city::obb &sel = map.buildings[static_cast<size_t>(view.selected)].box;
    view.focus = sel.center;
    view.focus_radius = std::max(160.0f, std::max(sel.half.x, sel.half.y) + 120.0f);
  }
  // Look up at the open floor, not at the street under it.
  state.cam_lift_goal = view.selected >= 0 || (cut_around && !view.cut.empty())
                            ? static_cast<f32>(view.floor) * city::floor_height * unit3d
                            : 0.0f;
  std::sort(view.cut.begin(), view.cut.end());
  view.cut.erase(std::unique(view.cut.begin(), view.cut.end()), view.cut.end());
}

} // namespace sandtable
