#pragma once

#include "city.h"

#include <vector>

// Drawing the city on the table (city.h has the data). view_build() turns a
// generated city into meshes and instance buffers once; view_draw() draws
// them every frame inside begin_3d()/end_3d(); view_draw_ui() draws the debug
// labels and the card for what is under the mouse, in screen pixels.

namespace sandtable::city {

// What the ground shows, for looking at the generator's work.
enum class overlay : u8 { none = 0, districts, blocks, foot, car, count };

const char *overlay_name(overlay o);

struct view_options {
  overlay layer = overlay::none;
  bool labels = true;   // district names from afar, shop names up close
  bool graph = false;   // the road graph: edges and junctions
  bool markers = false; // pins on businesses, gang seats and open places
  f32 night = 0.0f;     // 0 day to 1 night: lit windows, signs and lamps
  // Buildings drawn cut open: roof and upper floors off, ground floor walls
  // cut low, so what is inside shows (render_cutaway.cpp). Sorted.
  std::vector<i32> cut;
  i32 selected = -1;    // outlined, and first in `cut`
};

// The building under a screen point (the first its walls or roof meet, the
// way the camera sees it), or -1.
i32 view_pick(context &ctx, const city_map &map, vec2 screen);

void view_build(context &ctx, const city_map &map);
void view_draw(context &ctx, const city_map &map, const view_options &opt);
void view_draw_ui(context &ctx, const city_map &map, const view_options &opt, font_handle font);
void view_cleanup(context &ctx);

} // namespace sandtable::city
