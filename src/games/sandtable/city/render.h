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
  // The floor open to look into (0 the ground floor): the selected building's,
  // or, with `around`, every building's in `cut` that has it.
  i32 floor = 0;
  bool around = false;
  // Something looked at closely (a building picked, later a man or a car):
  // sharp within `sharp_radius` of it, blurred and hazy beyond (render.cpp,
  // post_fx::dof round a point); small detail drawn only within
  // `focus_radius`, which also spares the GPU.
  bool focused = false;
  vec2 focus{};
  f32 sharp_radius = 60.0f;
  f32 focus_radius = 220.0f;
  // Under the mouse, lit up (render_hover.cpp): the district (outlined, its
  // name large) and the building (framed); -1 for none.
  i32 hover_district = -1;
  i32 hover_building = -1;
  // The card telling what is under the mouse (render_debug.cpp): for looking
  // at the generator's work, off in play.
  bool debug_card = false;
  // The gangs' turf on the map: per block, the gang that holds it (-1 none),
  // drawn in `turf_colours[gang]`; rebuilt when `turf_version` changes.
  const std::vector<i8> *turf = nullptr;
  std::vector<rgba> turf_colours;
  u32 turf_version = 0;
  bool show_turf = false;
};

// What the last view_draw() drew, for the HUD: map chunks in view and with
// their full detail, and instances sent to the GPU.
struct view_stats {
  i32 chunks = 0, visible = 0, detailed = 0;
  u32 instances = 0;
};
const view_stats &view_last_stats();

// Whether a table point `margin` world units round is in view this frame (set
// by view_draw()): for anything drawn one by one, like the townsfolk.
bool view_sees(vec2 p, f32 margin = 20.0f);

// The building under a screen point (the first its walls or roof meet, the
// way the camera sees it), or -1.
i32 view_pick(context &ctx, const city_map &map, vec2 screen);

// Loads what does not depend on the city itself (the interior kit's
// models): once, at startup, before the first view_build().
void view_init(context &ctx);

void view_build(context &ctx, const city_map &map);
void view_draw(context &ctx, const city_map &map, const view_options &opt);
void view_draw_ui(context &ctx, const city_map &map, const view_options &opt, font_handle font);
void view_cleanup(context &ctx);
// Game exit only: what view_init() loaded and view_cleanup() does not touch.
void view_shutdown(context &ctx);

} // namespace sandtable::city
