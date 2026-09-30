#pragma once

// What the Vietnamese street has that the city kit (render_kit.cpp) has not:
// balconies with rails, pots and washing, and tiled roofs; and a building's
// frame and paint.

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

// Where the parts go: roofs and bodies (boxes); the fine parts, balconies
// and signs (detail); tanks (cylinders); lit windows and signs (glow, drawn
// unlit at night).
struct facade_batches {
  instances &boxes;
  instances &detail;
  instances &tanks;
  instances &glow;
};

// A balcony on the floor standing at `base`: railing or parapet, maybe pots
// and washing hung out.
void balcony(facade_batches &B, const frame &f, f32 base, rgba wall, u32 look, u32 salt);
// A tiled roof sloping to the front and back from a ridge along u.
void tiled_roof(facade_batches &B, const frame &f, f32 top, rgba col);

} // namespace sandtable::city
