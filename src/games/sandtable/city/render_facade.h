#pragma once

// The parts a street front is built from (render_facade.cpp): windows with
// frames and glass, balconies with railings, shutters, ledges, parapets,
// tiled roofs, air conditioners. render_buildings.cpp puts them together per
// kind of building.

#include "render_common.h"

namespace sandtable::city {

// A building's own frame: its middle, u along its front, v from the front to
// the back (the front is at -hy along v).
struct frame {
  vec2 c, u, v;
  f32 hx, hy;
  f32 angle;
  vec2 at(f32 a, f32 b) const { return c + u * a + v * b; }
  // A point on the front face at `a` along it, `out` world units in front.
  vec2 front(f32 a, f32 out) const { return at(a, -hy - out); }
};

frame frame_of(const building &b);

// A building's painted front and its bare concrete body, from its look.
rgba front_color(const building &b);
rgba concrete_color(const building &b);

// Where the parts go: the body, front and roof (boxes, always drawn); the
// fine parts, windows, balconies, shutters and signs (detail, drawn close
// up only); tanks (cylinders); lit windows and signs (glow, drawn at night).
struct facade_batches {
  instances &boxes;
  instances &detail;
  instances &tanks;
  instances &glow;
};

// A painted front over the whole face, over a bare concrete body.
void front_face(facade_batches &B, const frame &f, f32 height, rgba col);
// A thin ledge across the front at `base`.
void ledge(facade_batches &B, const frame &f, f32 base, rgba col);
// Windows across the front on the floor standing at `base`: `door` makes them
// tall doors onto a balcony. `salt` varies which are lit.
void window_row(facade_batches &B, const frame &f, f32 base, u32 look, u32 salt, bool door);
// A balcony on the floor standing at `base`: railing or parapet, maybe pots
// and washing hung out.
void balcony(facade_batches &B, const frame &f, f32 base, rgba wall, u32 look, u32 salt);
// The ground floor of a shop: open, dark inside, the shutter rolled up above,
// an awning and a sign; sometimes a tall sign up the front.
void shopfront(facade_batches &B, const frame &f, rgba sign, f32 height, u32 look);
// The ground floor of a home: a rolling steel shutter or a door and window.
void house_front(facade_batches &B, const frame &f, u32 look);
// The floors from `from` to `to` (exclusive) above the ground: a ledge,
// windows (doors where there is a balcony), the balcony, now and then an air
// conditioner.
void upper_floors(facade_batches &B, const building &b, const frame &f, rgba wall, i32 from, i32 to);
// A low wall round a flat roof at `top`.
void parapet(facade_batches &B, const frame &f, f32 top, rgba col);
// A tiled roof sloping to the front and back from a ridge along u.
void tiled_roof(facade_batches &B, const frame &f, f32 top, rgba col);
// An air conditioner hung on the front at `a`, `base` up.
void ac_unit(facade_batches &B, const frame &f, f32 a, f32 base);

} // namespace sandtable::city
