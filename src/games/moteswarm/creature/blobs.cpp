// The circles: where they sit this frame, how far they reach, and how out of
// round they leave the body.
//
// Nothing here is simulated. They are placed from the centre every frame and
// carry nothing between frames, which is why the shape never lags behind the
// body or wobbles after it stops.
#include "parts.h"

#include <algorithm>
#include <cmath>

namespace moteswarm {
// Where the circles are this frame. Written whole from the centre, so there is
// no state to carry: the same centre and the same breath give the same shape.
void place_blobs(creature &c, vec2 center, f32 breath_uniform, f32 breath_tall) {
  const f32 R = c.radius;
  const vec2 axis = c.breath_axis;
  for (blob &b : c.blobs) {
    // The breath draws the whole body out along breath_axis and narrows it
    // across, which is the rise and fall an eye actually reads as breathing.
    const f32 along = dot(b.off, axis) * (1.0f + breath_tall);
    const f32 across = cross(axis, b.off) * (1.0f - breath_tall);
    const vec2 off{along * axis.x - across * axis.y, along * axis.y + across * axis.x};
    b.pos = center + off * R;
    b.draw_radius = b.radius * R * (1.0f + breath_uniform);
  }
}

// How far the shape reaches from the centre along the heading and across it,
// read off the circles, which is the field the shader draws.
void measure_extents(creature &c, vec2 center) {
  f32 ahead = 0.0f;
  f32 behind = 0.0f;
  f32 side = 0.0f;
  for (const blob &b : c.blobs) {
    const vec2 d = b.pos - center;
    const f32 along = dot(d, c.dir);
    const f32 across = cross(c.dir, d);
    ahead = std::max(ahead, along + b.draw_radius);
    behind = std::max(behind, b.draw_radius - along);
    side = std::max(side, std::fabs(across) + b.draw_radius);
  }
  c.ahead = ahead;
  c.behind = behind;
  c.side = side;
}

// How far out of round the body is, along the heading against across it. The
// eye reads this to lean its lid the same way the body is leaning.
f32 body_aspect(const creature &c) {
  const f32 across = c.side * 2.0f;
  return across > 1e-4f ? (c.ahead + c.behind) / across : 1.0f;
}
} // namespace moteswarm
