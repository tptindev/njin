#include "render_lod.h"

#include "pbk.h"
#include "street_kit.h"

// Street furniture and the things in the open places: trees, poles, lamps,
// parked motorbikes, plastic stools, market stalls, containers, boats, and
// the railings along the bridges.
//
// At night the street lamps come on: every lamp in view gets its bulb and a
// warm pool of light on the ground (cheap, any number), and the ones nearest
// the middle of the view (or what is in focus) a real point light too, as
// many as the engine takes in one 3D pass (light3d_max), fading out toward
// the farthest of them so none pops on or off as the camera moves.

#include <algorithm>
#include <cmath>
#include <utility>

namespace sandtable::city {

namespace {

// The big things, drawn wherever they are in view: trees, containers, boats,
// stalls, railings, the monument.
chunked big_cubes, big_posts, leaves;
// The small ones, close up only: poles, lamps, bikes, stools, benches.
chunked cubes_s, posts_s, lights;
// The pools of light under the lamps, drawn at night only.
chunked pools;
chunked *all_batches[] = {&big_cubes, &big_posts, &leaves, &cubes_s, &posts_s, &lights, &pools};

// The street kit's furniture (street_kit.h): one batch and one model an
// asset, close up only like the small boxes they stand in for.
struct kit_batch {
  const street::asset *a = nullptr;
  model_handle model{};
  chunked c;
};
std::vector<kit_batch> kit;

kit_batch &kit_of(context &ctx, const street::asset &a) {
  for (kit_batch &k : kit)
    if (k.a == &a)
      return k;
  kit.push_back({&a, model_load(ctx, {.path = a.path.c_str(), .merge = true}), {}});
  kit_batch &k = kit.back();
  if (k.model.id == 0)
    NJIN_WARN("street: %s (%s) did not load", a.id.c_str(), a.path.c_str());
  // The file's colours are linear (glTF), the game's sRGB.
  for (i32 i = 0; k.model.id != 0 && i < model_material_count(ctx, k.model); ++i) {
    model_material mm = model_material_get(ctx, k.model, i);
    mm.color = pbk::linear_to_srgb(mm.color);
    if (mm.color.a >= 1.0f)
      mm.surface = {.specular = 0.08f, .shininess = 12.0f};
    model_material_set(ctx, k.model, i, mm);
  }
  return k;
}

// Every street lamp, for the real lights.
struct lamp {
  vec2 at;
  f32 height; // the bulb, world units up
  i32 chunk;
};
std::vector<lamp> lamps;

// How lit the lamps are drawn, 0 to 1: the bulbs' and pools' colours are
// written for it, and written again when the dusk has moved on enough.
f32 lamps_shown = -1.0f;

const rgba bulb_col = rgb8(255, 226, 160);
// A pool is three rings, each over the one before: brightest in the middle,
// fading out, so it has no hard edge. Radius share and `a` when fully on.
const rgba pool_col{1.0f, 0.74f, 0.38f, 1.0f};
constexpr f32 pool_rings[3][2] = {{1.0f, 0.07f}, {0.66f, 0.08f}, {0.36f, 0.1f}};
const rgba lamp_light{1.0f, 0.84f, 0.6f, 1.0f};
constexpr f32 lamp_height = 17.0f;   // the bulb, world units up
constexpr f32 pool_radius = 30.0f;   // world units
constexpr f32 light_reach = 110.0f;  // world units
constexpr i32 lamp_lights = 12;      // real lights; the rest of light3d_max stays free
constexpr f32 lamp_light_range = 900.0f; // none farther than this from the middle

f32 smoothstep(f32 e0, f32 e1, f32 x) {
  const f32 t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

const rgba greens[] = {rgb8(70, 120, 56), rgb8(88, 136, 60), rgb8(60, 108, 62), rgb8(104, 146, 70)};
const rgba bikes[] = {rgb8(180, 40, 36), rgb8(30, 30, 34), rgb8(40, 80, 160), rgb8(220, 220, 220), rgb8(120, 120, 124)};
const rgba stools[] = {rgb8(210, 40, 40), rgb8(40, 90, 200), rgb8(230, 180, 40)};
const rgba boxes_col[] = {rgb8(180, 60, 40), rgb8(40, 90, 150), rgb8(60, 130, 80), rgb8(210, 130, 40),
                          rgb8(140, 140, 146)};
const rgba canvas[] = {rgb8(200, 50, 44), rgb8(40, 90, 170), rgb8(230, 200, 60), rgb8(60, 150, 80)};

// A prop of the street kit at its real size: its origin on the ground at
// the prop, its +X along the prop's angle. False for the kinds the kit has not got.
bool add_kit(context &ctx, const prop &p, i32 chunk) {
  const street::asset *a = street::asset_for(p);
  if (!a)
    return false;
  kit_batch &k = kit_of(ctx, *a);
  if (k.c.start.empty()) {
    k.c.begin(chunk_count());
    for (i32 c = 0; c <= chunk; ++c)
      k.c.mark(c);
  }
  const f32 s = units_per_metre * unit3d;
  k.c.inst.add3(to3d(p.pos), {s, s, s}, colors::white, -p.angle);
  if (p.kind == prop_kind::lamp) {
    // The bulb under the arm's end, its pool of light on the ground below.
    const obb o{p.pos, {}, p.angle};
    const vec2 at = p.pos + (o.axis_x() * a->light.x + o.axis_y() * a->light.z) * units_per_metre;
    const f32 h = a->light.y * units_per_metre;
    lights.inst.ball(at, h - 0.6f, 1.2f, bulb_col);
    for (const auto &ring : pool_rings)
      pools.inst.post(at, 0.05f / unit3d, pool_radius * ring[0], 0.002f / unit3d, pool_col);
    lamps.push_back({at, h, chunk});
  }
  return true;
}

void add(const prop &p) {
  const bool small = p.kind == prop_kind::lamp || p.kind == prop_kind::pole || p.kind == prop_kind::motorbike ||
                     p.kind == prop_kind::stool || p.kind == prop_kind::bench;
  instances &cubes = small ? cubes_s.inst : big_cubes.inst;
  instances &posts = small ? posts_s.inst : big_posts.inst;
  const vec2 at = p.pos;
  const f32 s = p.scale;
  switch (p.kind) {
  case prop_kind::tree:
    posts.post(at, 0.0f, 1.0f * s, 9.0f * s, rgb8(96, 72, 50));
    leaves.inst.ball(at, 13.0f * s, 7.0f * s, pick(greens, p.look, 1));
    break;
  case prop_kind::lamp:
    posts.post(at, 0.0f, 0.5f, 18.0f, rgb8(70, 72, 76));
    cubes.box(at, 18.0f, {5.0f, 1.0f, 1.6f}, p.angle, rgb8(70, 72, 76));
    lights.inst.ball(at, 17.5f, 1.4f, bulb_col);
    // Flat on the ground, just over the road markings.
    for (const auto &ring : pool_rings)
      pools.inst.post(at, 0.05f / unit3d, pool_radius * ring[0], 0.002f / unit3d, pool_col);
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

// Low concrete railings along both edges of a bridge.
void railings(const spot &s) {
  {
    for (const f32 side : {-1.0f, 1.0f})
      big_cubes.inst.box(s.box.center + s.box.axis_y() * (side * (s.box.half.y - 1.0f)), 0.0f,
                {s.box.half.x * 2.0f + 8.0f, 3.5f, 1.4f}, s.box.angle, rgb8(200, 196, 188));
  }
}

} // namespace

void props_build(context &ctx, const city_map &map) {
  const i32 chunks = chunk_count();
  for (chunked *c : all_batches)
    c->begin(chunks);
  lamps.clear();
  lamps_shown = -1.0f;
  std::vector<std::vector<i32>> in_chunk(static_cast<size_t>(chunks));
  for (i32 i = 0; i < static_cast<i32>(map.props.size()); ++i)
    in_chunk[static_cast<size_t>(chunk_of(map.props[static_cast<size_t>(i)].pos))].push_back(i);
  std::vector<std::vector<i32>> bridges(static_cast<size_t>(chunks));
  for (i32 i = 0; i < static_cast<i32>(map.spots.size()); ++i)
    if (map.spots[static_cast<size_t>(i)].kind == spot_kind::bridge)
      bridges[static_cast<size_t>(chunk_of(map.spots[static_cast<size_t>(i)].box.center))].push_back(i);
  for (i32 ch = 0; ch < chunks; ++ch) {
    for (chunked *c : all_batches)
      c->mark(ch);
    for (kit_batch &k : kit)
      if (!k.c.start.empty())
        k.c.mark(ch);
    for (const i32 i : in_chunk[static_cast<size_t>(ch)]) {
      const prop &pr = map.props[static_cast<size_t>(i)];
      if (add_kit(ctx, pr, ch))
        continue;
      add(pr);
      if (pr.kind == prop_kind::lamp)
        lamps.push_back({pr.pos, lamp_height, ch});
    }
    for (const i32 i : bridges[static_cast<size_t>(ch)])
      railings(map.spots[static_cast<size_t>(i)]);
  }
  for (chunked *c : all_batches) {
    c->end();
    c->inst.upload(ctx);
  }
  for (kit_batch &k : kit)
    if (!k.c.start.empty()) {
      k.c.end();
      k.c.inst.upload(ctx);
    }
}

namespace {

// The bulbs dim to bright and the pools fade in as `on` goes 0 to 1.
void shade_lamps(context &ctx, f32 on) {
  if (std::fabs(on - lamps_shown) < 0.02f && !(on == 1.0f && lamps_shown != 1.0f))
    return;
  lamps_shown = on;
  const f32 bright = 0.3f + 0.7f * on;
  for (size_t i = 0; i + 16 <= lights.inst.data.size(); i += 16) {
    lights.inst.data[i + 4] = bulb_col.r * bright;
    lights.inst.data[i + 5] = bulb_col.g * bright;
    lights.inst.data[i + 6] = bulb_col.b * bright;
  }
  for (size_t i = 0; i + 16 <= pools.inst.data.size(); i += 16)
    pools.inst.data[i + 7] = pool_rings[(i / 16) % 3][1] * on;
  lights.inst.upload(ctx);
  pools.inst.upload(ctx);
}

// Real lights on the lamps nearest the middle of the view.
void light_lamps(context &ctx, f32 on) {
  std::vector<std::pair<f32, const lamp *>> near;
  for (const lamp &l : lamps) {
    if (!chunk_visible(l.chunk))
      continue;
    const f32 d = distance(l.at, cull().center);
    if (d < lamp_light_range)
      near.emplace_back(d, &l);
  }
  const size_t n = std::min(near.size(), static_cast<size_t>(lamp_lights) + 1);
  std::partial_sort(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(n), near.end(),
                    [](const auto &a, const auto &b) { return a.first < b.first; });
  // The first lamp left out marks where the lights have faded to nothing.
  const f32 edge = near.size() > static_cast<size_t>(lamp_lights) ? near[lamp_lights].first : lamp_light_range;
  for (size_t i = 0; i < std::min(near.size(), static_cast<size_t>(lamp_lights)); ++i) {
    const f32 fade = 1.0f - smoothstep(edge * 0.6f, edge, near[i].first);
    if (fade <= 0.0f)
      continue;
    light3d_add(ctx, {.position = to3d(near[i].second->at, near[i].second->height * unit3d),
                      .color = lamp_light,
                      .intensity = 1.8f * on * fade,
                      .radius = light_reach * unit3d});
  }
}

} // namespace

void props_draw(context &ctx, const view_options &opt) {
  const f32 prop_r = cull().prop_r;
  const auto all = [](i32) { return true; };
  const auto near = [prop_r](i32 c) { return chunk_detailed(c, prop_r); };
  material3d_set(ctx, {.specular = 0.1f, .shininess = 14.0f});
  draw_chunks(ctx, big_cubes, mesh3d_cube, all);
  draw_chunks(ctx, big_posts, mesh3d_cylinder_low, all);
  draw_chunks(ctx, cubes_s, mesh3d_cube, near);
  draw_chunks(ctx, posts_s, mesh3d_cylinder_low, near);
  for (const kit_batch &k : kit)
    draw_chunks_model(ctx, k.c, k.model, near);
  material3d_set(ctx, {.specular = 0.05f, .shininess = 6.0f, .rim = {0.8f, 1.0f, 0.7f, 0.1f}});
  draw_chunks(ctx, leaves, mesh3d_sphere_low, all);
  // The lamps come on as the light goes, all of them in view.
  const f32 on = smoothstep(0.25f, 0.45f, opt.night);
  if (on > 0.01f) {
    shade_lamps(ctx, on);
    light_lamps(ctx, on);
    material3d_set(ctx, {.unlit = true, .cast_shadows = false});
    draw_chunks(ctx, lights, mesh3d_sphere_low, all);
    draw_chunks(ctx, pools, mesh3d_cylinder_low, all);
  }
  material3d_set(ctx, {});
}

void props_cleanup(context &ctx) {
  for (chunked *c : all_batches) {
    c->inst.destroy(ctx);
    c->start.clear();
  }
  for (kit_batch &k : kit) {
    k.c.inst.destroy(ctx);
    if (k.model.id != 0)
      model_unload(ctx, k.model);
  }
  kit.clear();
}

} // namespace sandtable::city
