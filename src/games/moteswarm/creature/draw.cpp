// Flattening a creature into what the shader wants. Pure: everything here only
// reads the creature, so a draw may be built from a render pass with no
// ordering to worry about.
#include "parts.h"

#include <algorithm>
#include <cmath>

namespace moteswarm {
namespace {
void grow_bounds(vec2 &lo, vec2 &hi, vec2 p, f32 r) {
  lo = {std::min(lo.x, p.x - r), std::min(lo.y, p.y - r)};
  hi = {std::max(hi.x, p.x + r), std::max(hi.y, p.y + r)};
}
} // namespace

void creature_build_draw(const creature &c, creature_draw &out) {
  const f32 R = c.radius;
  vec2 lo{1e9f, 1e9f};
  vec2 hi{-1e9f, -1e9f};

  for (i32 i = 0; i < blob_count; i++) {
    const blob &b = c.blobs[i];
    out.lump[i] = {b.pos.x, b.pos.y, b.draw_radius, 0.0f};
    grow_bounds(lo, hi, b.pos, b.draw_radius);
  }
  out.blend = R * c.blend_k;

  out.tail_count = c.tail_count;
  for (i32 i = 0; i < c.tail_count; i++) {
    const tail &t = c.tails[i];
    out.tail_pts[i] = {t.root.x, t.root.y, t.tip.x, t.tip.y};
    out.tail_r[i] = {t.root_r, t.tip_r};
    grow_bounds(lo, hi, t.root, t.root_r);
    grow_bounds(lo, hi, t.tip, t.tip_r);
  }
  out.tail_blend = R * c.tail_blend_k;
  out.tail_pair_blend = R * c.tail_pair_blend_k;

  out.shadow_offset = c.shadow_offset * R;
  out.shadow_blur = R * c.shadow_blur;
  out.shadow_alpha = c.shadow_alpha;

  out.eye_pos = c.eye.pos;
  out.pupil_pos = c.eye.pupil;
  out.eye_radius = c.eye.sclera_r;
  out.pupil_radius = c.eye.pupil_r;
  out.eye_open = c.eye.open;
  out.eye_stretch = 1.0f + (body_aspect(c) - 1.0f) * c.eye_stretch_mix;
  out.dir = c.dir;

  // The eye is not one of the circles and is the one part that can sit outside
  // them: it leans into the nose and leads the body's motion, so at speed it
  // reaches past the silhouette. Left out of the box, the quad would not cover
  // it and it would not be drawn.
  grow_bounds(lo, hi, out.eye_pos, out.eye_radius);
  grow_bounds(lo, hi, out.pupil_pos, out.pupil_radius);

  // Every blend pushes the visible edge outward past the circles it joins, and
  // the shadow reaches further still, so the box grows by both plus a pixel of
  // anti-aliasing.
  const f32 widest = std::max({out.blend, out.tail_blend, out.tail_pair_blend});
  const f32 thrown = std::max(std::fabs(out.shadow_offset.x), std::fabs(out.shadow_offset.y));
  const f32 pad = widest + thrown + out.shadow_blur + 2.0f;
  out.bounds_min = {lo.x - pad, lo.y - pad};
  out.bounds_max = {hi.x + pad, hi.y + pad};
}
} // namespace moteswarm
