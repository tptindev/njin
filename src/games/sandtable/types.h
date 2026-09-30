#pragma once

#include <njin.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace sandtable {
using namespace njin;

// The table, in world units: x to the right and y toward the player. The
// city (world.h) covers all of it; a man is about 20 units tall.
inline constexpr f32 world_width = 3200.0f;
inline constexpr f32 world_height = 2048.0f;

constexpr rgba rgb(i32 r, i32 g, i32 b, i32 a = 255) {
  return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

// Palette
inline constexpr rgba col_bg_dark = rgb(20, 18, 16);
inline constexpr rgba col_sand = rgb(196, 170, 124);
inline constexpr rgba col_sand_dark = rgb(172, 146, 102);
inline constexpr rgba col_frame = rgb(92, 60, 34);
inline constexpr rgba col_frame_light = rgb(140, 98, 58);

inline constexpr rgba col_gold = rgb(255, 208, 64);
inline constexpr rgba col_gold_light = rgb(255, 235, 130);
inline constexpr rgba col_white = rgb(245, 248, 250);
inline constexpr rgba col_muted = rgb(155, 168, 176);
inline constexpr rgba col_good = rgb(82, 215, 120);
inline constexpr rgba col_warn = rgb(240, 195, 55);
inline constexpr rgba col_bad = rgb(235, 65, 60);

struct game_state {
  f32 hour = 10.0f; // time of day, 0 to 24 (weather.h)
  i32 day = 1;       // the gang's calendar (gang.h)
  f32 speed = 1.0f;  // how fast the town's clock and the gang's men go: 0, 1 or 3
  bool popup_open = false; // a HUD popup is up (hud.h): the clock stops, the map takes no input
  u32 seed = 1;      // the city's (city.h)

  // The camera (view.h): it looks at `cam_target` (table coordinates) from
  // `cam_distance` 3D units away, turned `cam_yaw` degrees round it (0 is from
  // the player's side). The controls move the goals; the camera eases to them.
  vec2 cam_target{world_width * 0.5f, world_height * 0.5f};
  vec2 cam_target_goal{world_width * 0.5f, world_height * 0.5f};
  f32 cam_yaw = 0.0f;
  f32 cam_yaw_goal = 0.0f;
  f32 cam_distance = 80.0f;
  f32 cam_distance_goal = 80.0f;
  // How high (3D units) the camera looks: up to the floor of a building open
  // to look into (world.cpp), eased there.
  f32 cam_lift = 0.0f;
  f32 cam_lift_goal = 0.0f;
  // 0 the usual slant, 1 looking steeply down at what is in focus, so the
  // houses in front of it do not hide it; eased.
  f32 cam_steep = 0.0f;
  f32 cam_steep_goal = 0.0f;
};

extern game_state state;

} // namespace sandtable
