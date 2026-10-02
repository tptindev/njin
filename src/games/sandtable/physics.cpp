#include "physics.h"

#include "city/street_kit.h"
#include "city/railings.h"
#include "city/render_common.h"

#include <vector>

namespace sandtable {

namespace {

std::vector<body3d_handle> bodies;
std::vector<body3d_handle> building_box; // one a building, 0 once dropped

// A box standing on the ground at `at`, `size` world units (along `angle`,
// height, across), from `base` up.
void box(context &ctx, vec2 at, f32 base, vec3 size, f32 angle) {
  const body3d_handle h = body3d_create(ctx, {.shape = shape3d_box,
                                              .position = to_phys(at, base + size.y * 0.5f),
                                              .rotation = {0.0f, -angle, 0.0f},
                                              .size = size * metres_per_unit});
  if (h.id != 0)
    bodies.push_back(h);
}

// An upright post, `radius` and `height` world units.
void post(context &ctx, vec2 at, f32 radius, f32 height) {
  const body3d_handle h = body3d_create(ctx, {.shape = shape3d_cylinder,
                                              .position = to_phys(at, height * 0.5f),
                                              .radius = radius * metres_per_unit,
                                              .height = height * metres_per_unit});
  if (h.id != 0)
    bodies.push_back(h);
}

// What stands in the way, the same sizes as render_props.cpp draws.
void prop_body(context &ctx, const city::prop &p) {
  // The street kit's furniture at its real size: its footprint from the
  // kit's manifest (a lamp's or a pole's post only), its full height.
  if (const city::street::asset *a = city::street::asset_for(p)) {
    const city::obb f = city::street::footprint(p, *a);
    const f32 h = a->height * city::units_per_metre;
    if (a->post)
      post(ctx, f.center, f.half.x, h);
    else
      box(ctx, f.center, 0.0f, {f.half.x * 2.0f, h, f.half.y * 2.0f}, f.angle);
    return;
  }
  const f32 s = p.scale;
  switch (p.kind) {
  case city::prop_kind::tree: post(ctx, p.pos, 1.0f * s, 9.0f * s); break;
  case city::prop_kind::lamp: post(ctx, p.pos, 0.5f, 18.0f); break;
  case city::prop_kind::pole: post(ctx, p.pos, 0.7f * s, 20.0f * s); break;
  case city::prop_kind::motorbike: box(ctx, p.pos, 0.0f, {7.0f, 4.8f, 2.2f}, p.angle); break;
  case city::prop_kind::stool: box(ctx, p.pos, 0.0f, {2.2f, 2.6f, 2.2f}, p.angle); break;
  case city::prop_kind::stall: box(ctx, p.pos, 0.0f, {11.0f, 4.0f, 7.0f}, p.angle); break;
  case city::prop_kind::container:
    box(ctx, p.pos, 0.0f, {30.0f, 11.0f * p.scale, 11.0f}, p.angle);
    break;
  case city::prop_kind::bench: box(ctx, p.pos, 0.0f, {8.0f, 2.7f, 3.0f}, p.angle); break;
  case city::prop_kind::monument:
    post(ctx, p.pos, 10.0f, 4.0f);
    box(ctx, p.pos, 4.0f, {5.0f, 34.0f, 5.0f}, 45.0f);
    break;
  default: break; // boats are on the water
  }
}

} // namespace

void physics_clear(context &ctx) {
  for (const body3d_handle h : bodies)
    body3d_destroy(ctx, h);
  bodies.clear();
  for (const body3d_handle h : building_box)
    if (h.id != 0)
      body3d_destroy(ctx, h);
  building_box.clear();
}

bool physics_drop_building(context &ctx, i32 i) {
  if (i < 0 || i >= static_cast<i32>(building_box.size()) || building_box[static_cast<size_t>(i)].id == 0)
    return false;
  body3d_destroy(ctx, building_box[static_cast<size_t>(i)]);
  building_box[static_cast<size_t>(i)] = {};
  return true;
}

void physics_build(context &ctx, const city::city_map &map) {
  physics_clear(ctx);
  // The ground, a slab whose top is the street.
  const vec2 mid{map.desc.width * 0.5f, map.desc.height * 0.5f};
  box(ctx, mid, -20.0f, {map.desc.width + 400.0f, 20.0f, map.desc.height + 400.0f}, 0.0f);
  for (const city::building &b : map.buildings) {
    box(ctx, b.box.center, 0.0f, {b.box.half.x * 2.0f, b.height, b.box.half.y * 2.0f}, b.box.angle);
    building_box.push_back(bodies.empty() ? body3d_handle{} : bodies.back());
    if (!bodies.empty())
      bodies.pop_back();
  }
  for (const city::prop &p : map.props)
    prop_body(ctx, p);
  // One thin edge barrier, exactly the same route and height as the model.
  for (const city::railing_edge &e : city::railing_layout(map))
    box(ctx, (e.a + e.b) * 0.5f, city::layer_sidewalk / unit3d,
        {distance(e.a, e.b) + city::railing_thickness, city::railing_height, city::railing_thickness},
        angle_of(e.b - e.a));
}

character3d_handle physics_person(context &ctx, vec2 at) {
  // Shoulders about 45 cm across; a kerb or a step of 30 cm is stepped up.
  return character3d_create(ctx, {.position = to_phys(at),
                                  .radius = 0.22f,
                                  .height = city::person_height * metres_per_unit,
                                  .max_slope = 50.0f,
                                  .step_height = 0.3f,
                                  .mass = 70.0f});
}

void physics_seated(context &ctx, vec2 at) {
  const body3d_handle h = body3d_create(ctx, {.shape = shape3d_cylinder,
                                              .position = to_phys(at, city::person_height * 0.4f),
                                              .radius = 0.35f,
                                              .height = city::person_height * 0.8f * metres_per_unit});
  if (h.id != 0)
    bodies.push_back(h);
}

} // namespace sandtable
