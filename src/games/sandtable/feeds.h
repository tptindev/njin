#pragma once

#include "types.h"

#include <vector>

namespace sandtable {

// What the player's men out on a job see, live: a camera over each one's
// shoulder, drawn into a render texture of its own (begin_3d into a target),
// for the HUD's camera wall (hud.cpp). A few views are redrawn each frame, in
// turn, as each is a whole extra pass of the town.

struct feed {
  i32 man = -1;                 // in gang().men
  render_texture_handle view{}; // what he sees, `size` pixels
  vec2 size{};
  bool fresh = false; // drawn at least once
};

// The wall is up: views are kept and drawn. Off, they are let go.
bool &feeds_on();
// The men of the player's gang on a job now, one view each, in their order.
const std::vector<feed> &feeds();
// The size a view is drawn at, pixels: the wall's cell.
void feeds_set_size(vec2 cell);
// In phase_render, after the table's own 3D pass.
void feeds_render(context &ctx);
void feeds_cleanup(context &ctx);

} // namespace sandtable
