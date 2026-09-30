#include "render_common.h"

// Street furniture and the things in the open places: trees, poles, lamps,
// parked motorbikes, plastic stools, market stalls, containers, boats, and
// the railings along the bridges.

namespace sandtable::city {

namespace {

instances cubes;   // most things are boxes
instances posts;   // trunks, poles, the monument
instances leaves;  // tree tops
instances lights;  // lamp heads, lit at night

const rgba greens[] = {rgb8(70, 120, 56), rgb8(88, 136, 60), rgb8(60, 108, 62), rgb8(104, 146, 70)};
const rgba bikes[] = {rgb8(180, 40, 36), rgb8(30, 30, 34), rgb8(40, 80, 160), rgb8(220, 220, 220), rgb8(120, 120, 124)};
const rgba stools[] = {rgb8(210, 40, 40), rgb8(40, 90, 200), rgb8(230, 180, 40)};
const rgba boxes_col[] = {rgb8(180, 60, 40), rgb8(40, 90, 150), rgb8(60, 130, 80), rgb8(210, 130, 40),
                          rgb8(140, 140, 146)};
const rgba canvas[] = {rgb8(200, 50, 44), rgb8(40, 90, 170), rgb8(230, 200, 60), rgb8(60, 150, 80)};

void add(const prop &p) {
  const vec2 at = p.pos;
  const f32 s = p.scale;
  switch (p.kind) {
  case prop_kind::tree:
    posts.post(at, 0.0f, 1.0f * s, 9.0f * s, rgb8(96, 72, 50));
    leaves.ball(at, 13.0f * s, 7.0f * s, pick(greens, p.look, 1));
    break;
  case prop_kind::lamp:
    posts.post(at, 0.0f, 0.5f, 18.0f, rgb8(70, 72, 76));
    cubes.box(at, 18.0f, {5.0f, 1.0f, 1.6f}, p.angle, rgb8(70, 72, 76));
    lights.ball(at, 17.5f, 1.4f, rgb8(255, 226, 160));
    break;
  case prop_kind::pole:
    posts.post(at, 0.0f, 0.7f * s, 20.0f * s, rgb8(128, 124, 118));
    cubes.box(at, 18.0f * s, {9.0f * s, 0.7f, 0.7f}, p.angle + 90.0f, rgb8(90, 88, 84));
    break;
  case prop_kind::motorbike:
    cubes.box(at, 1.2f, {7.0f, 2.8f, 2.2f}, p.angle, pick(bikes, p.look, 1));
    cubes.box(at - from_angle(p.angle) * 1.0f, 4.0f, {3.2f, 0.8f, 1.8f}, p.angle, rgb8(30, 28, 26));
    break;
  case prop_kind::stool:
    cubes.box(at, 0.0f, {2.2f, 2.6f, 2.2f}, p.angle, pick(stools, p.look, 1));
    break;
  case prop_kind::stall:
    cubes.box(at, 0.0f, {11.0f, 4.0f, 7.0f}, p.angle, rgb8(150, 120, 86));
    cubes.box(at, 9.0f, {13.0f, 0.8f, 9.0f}, p.angle, pick(canvas, p.look, 1));
    break;
  case prop_kind::container: {
    const i32 stack = static_cast<i32>(p.scale);
    for (i32 k = 0; k < stack; ++k)
      cubes.box(at, static_cast<f32>(k) * 11.0f, {30.0f, 11.0f, 11.0f}, p.angle, pick(boxes_col, p.look, 1 + k));
    break;
  }
  case prop_kind::boat:
    cubes.box(at, -1.0f, {26.0f * s, 3.0f, 8.0f * s}, p.angle, rgb8(110, 78, 50));
    cubes.box(at - from_angle(p.angle) * 4.0f, 2.0f, {8.0f * s, 4.0f, 6.0f * s}, p.angle, rgb8(60, 100, 90));
    break;
  case prop_kind::bench:
    cubes.box(at, 1.5f, {8.0f, 1.2f, 3.0f}, p.angle, rgb8(120, 90, 60));
    break;
  case prop_kind::monument:
    posts.post(at, 0.0f, 10.0f, 4.0f, rgb8(200, 196, 186));
    cubes.box(at, 4.0f, {5.0f, 34.0f, 5.0f}, 45.0f, rgb8(226, 220, 204));
    cubes.box(at, 38.0f, {2.0f, 4.0f, 2.0f}, 45.0f, rgb8(210, 170, 60));
    break;
  default:
    break;
  }
}

// Low concrete railings along both edges of every bridge.
void railings(const city_map &map) {
  for (const spot &s : map.spots) {
    if (s.kind != spot_kind::bridge)
      continue;
    for (const f32 side : {-1.0f, 1.0f})
      cubes.box(s.box.center + s.box.axis_y() * (side * (s.box.half.y - 1.0f)), 0.0f,
                {s.box.half.x * 2.0f + 8.0f, 3.5f, 1.4f}, s.box.angle, rgb8(200, 196, 188));
  }
}

} // namespace

void props_build(context &ctx, const city_map &map) {
  cubes.clear();
  posts.clear();
  leaves.clear();
  lights.clear();
  for (const prop &p : map.props)
    add(p);
  railings(map);
  cubes.upload(ctx);
  posts.upload(ctx);
  leaves.upload(ctx);
  lights.upload(ctx);
}

void props_draw(context &ctx, const view_options &opt) {
  material3d_set(ctx, {.specular = 0.1f, .shininess = 14.0f});
  cubes.draw(ctx, mesh3d_cube);
  posts.draw(ctx, mesh3d_cylinder_low);
  material3d_set(ctx, {.specular = 0.05f, .shininess = 6.0f, .rim = {0.8f, 1.0f, 0.7f, 0.1f}});
  leaves.draw(ctx, mesh3d_sphere_low);
  if (opt.night > 0.3f) {
    material3d_set(ctx, {.unlit = true, .cast_shadows = false});
    lights.draw(ctx, mesh3d_sphere_low);
  }
  material3d_set(ctx, {});
}

void props_cleanup(context &ctx) {
  cubes.destroy(ctx);
  posts.destroy(ctx);
  leaves.destroy(ctx);
  lights.destroy(ctx);
}

} // namespace sandtable::city
