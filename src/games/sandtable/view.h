#pragma once

#include "types.h"

namespace sandtable {

// The table in 3D, and the camera over it.
//
// The game works in table coordinates: world units on a flat table, x to the
// right and y toward the player. In 3D 32 world units are one unit, table x
// is 3D x, table y is 3D z, and 3D y is height; the sand is at height 0. The
// camera is an RTS one: it looks down at a point of the table from a slant,
// and can pan, turn round that point and come closer.

inline constexpr f32 unit3d = 1.0f / 32.0f; // 3D units per world unit

inline vec3 to3d(vec2 p, f32 height = 0.0f) { return {p.x * unit3d, height, p.y * unit3d}; }

// The camera for this frame.
camera3d table_camera();
// Moves the camera from the keyboard and mouse: WASD or the arrows to pan
// (Shift faster), Q/E to turn, the wheel to come closer, the middle button to
// drag the table. `in_hud` is when the mouse is over the UI: no wheel then.
void update_view(context &ctx, bool in_hud);
// Looks at `at` from `distance`; `snap` jumps there instead of easing.
void view_focus(vec2 at, f32 distance, bool snap = false);
// Back to the whole table, seen from the player's side.
void view_reset();

// The table point under a screen point, false when it misses the table.
bool screen_to_table(context &ctx, vec2 screen, vec2 *at);
bool mouse_on_table(context &ctx, vec2 *at);
// Where a table point, `lift` 3D units above the sand, is on screen.
vec2 table_to_screen(context &ctx, vec2 p, f32 lift = 0.0f, bool *visible = nullptr);

} // namespace sandtable
