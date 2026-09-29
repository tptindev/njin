#include "fps.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// A port of Biped-Potato's Bevy FPS tutorial (part 2) to njin, with its gun
// model (assets/models/LICENSE.txt):
//   camera     mouse look with the cursor locked, pitch clamped
//   movement   WASD at a fixed step, gravity, box collision against the level
//   shooting   a ray from the eye through the crosshair (ray3d_box), nearest
//              hit wins
//   tracers    a streak from the muzzle to the hit point
//   targets    "grid shot": 5 targets on a 5x5 grid, a hit moves one
//   effects    shadows from the sun, fog, two coloured lamps, a flashlight
//              (F), a muzzle light, glowing tracers with bloom, sparks where
//              a shot lands, and a hit target flashes, bursts and dissolves
//              while the camera kicks
//   gizmos     G shows the debug view: target hit boxes, each shot's path for
//              a second, a label where it hit (njin_gizmo.h)
// Units are the tutorial's: the player is 20 tall, the floor 2000 wide.

namespace fps {
namespace {
using namespace njin;

constexpr f32 fov = 103.0f;          // vertical, degrees
constexpr f32 sensitivity = 0.035f;  // degrees per mouse pixel
constexpr f32 pitch_limit = 88.0f;
constexpr f32 move_speed = 20.0f;
constexpr f32 gravity = 9.8f;
constexpr vec3 player_half{1.0f, 10.0f, 1.0f};
constexpr vec3 spawn{0.0f, 30.0f, 0.0f};
constexpr f32 tracer_speed = 300.0f;
// Where the barrel ends, in the camera's frame (from the tutorial's Blender
// scene, converted to y up).
constexpr vec3 muzzle_local{0.530462f, -0.466568f, -2.10557f};

constexpr i32 grid_size = 5;
constexpr f32 cell_size = 5.0f;
constexpr i32 target_count = 5;
constexpr f32 target_z = -40.0f;
constexpr f32 target_radius = cell_size / 8.0f;
// The tutorial hits a box inside the sphere, not the sphere itself.
constexpr f32 target_half = target_radius * 0.70710678f;

constexpr rgba level_color{1.0f, 1.0f, 1.0f, 1.0f};
// A little below white, so bloom picks up only what glows.
constexpr rgba floor_color{0.72f, 0.72f, 0.74f, 1.0f};
constexpr rgba target_color{1.0f, 0.0f, 0.0f, 1.0f};
constexpr rgba tracer_color{1.0f, 1.0f, 0.0f, 1.0f};
constexpr rgba crosshair_color{0.0f, 1.0f, 0.0f, 1.0f};
constexpr rgba fog_color{0.17f, 0.17f, 0.18f, 1.0f}; // the clear colour
constexpr f32 muzzle_time = 0.06f;
constexpr f32 ghost_time = 0.4f; // a hit target dissolves this long
// Emitter units (pixels, as the njin::fx presets) to world units.
constexpr f32 fx_scale = 0.06f;

// Two lamps on the front corners of the big cube.
struct lamp {
  vec3 pos;
  rgba color;
};
const lamp lamps[] = {
    {{-34.0f, 8.0f, -68.0f}, {0.35f, 0.6f, 1.0f, 1.0f}},
    {{34.0f, 8.0f, -68.0f}, {1.0f, 0.55f, 0.2f, 1.0f}},
};

// An axis-aligned box, by centre and half size.
struct box {
  vec3 center;
  vec3 half;
};

struct target {
  vec3 pos;
  bool dead = true; // waiting for a new cell (all start that way)
};

// Where a target was hit: it flashes and dissolves there while the target
// itself has already moved on.
struct ghost {
  vec3 pos;
  f32 age = 0.0f;
  f32 seed = 0.0f;
};

struct tracer {
  vec3 from;
  vec3 to;
  f32 lifetime;
  f32 age = 0.0f;
};

struct game_state {
  vec3 pos = spawn; // centre of the player's box; the eye is here too
  vec3 velocity{};
  bool grounded = false;
  f32 yaw = 0.0f;   // degrees, around +y
  f32 pitch = 0.0f; // degrees, around the camera's x
  bool locked = true;
  std::vector<target> targets;
  std::vector<tracer> tracers;
  std::vector<ghost> ghosts;
  f32 muzzle = 0.0f; // time left on the muzzle flash
  vec3 muzzle_pos{};
  bool flashlight = false;
  i32 hits = 0;
  model_handle gun;
};
game_state g;

// The level: a floor with no thickness and one big cube.
const box level_boxes[] = {
    {{0.0f, 0.0f, 0.0f}, {1000.0f, 0.0f, 1000.0f}},
    {{0.0f, 0.0f, -100.0f}, {30.0f, 30.0f, 30.0f}},
};

// Camera basis from yaw then pitch (the same rotation draw_model applies).
vec3 forward_of(f32 yaw, f32 pitch) {
  const f32 y = yaw * (pi / 180.0f);
  const f32 p = pitch * (pi / 180.0f);
  return {-std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
}
vec3 right_of(f32 yaw) {
  const f32 y = yaw * (pi / 180.0f);
  return {std::cos(y), 0.0f, -std::sin(y)};
}

bool overlaps(vec3 pos, vec3 half, const box &b) {
  return std::fabs(pos.x - b.center.x) < half.x + b.half.x &&
         std::fabs(pos.y - b.center.y) < half.y + b.half.y &&
         std::fabs(pos.z - b.center.z) < half.z + b.half.z;
}

// Everything the player collides with: the level and the live targets.
std::vector<box> solids() {
  std::vector<box> out(std::begin(level_boxes), std::end(level_boxes));
  for (const target &t : g.targets) {
    if (!t.dead)
      out.push_back({t.pos, {target_half, target_half, target_half}});
  }
  return out;
}

// Moves one axis and pushes back out of whatever it ran into. Returns true
// when the move was stopped.
bool move_axis(f32 vec3::*axis, f32 amount, const std::vector<box> &boxes) {
  g.pos.*axis += amount;
  bool blocked = false;
  for (const box &b : boxes) {
    if (!overlaps(g.pos, player_half, b))
      continue;
    const f32 reach = player_half.*axis + b.half.*axis;
    g.pos.*axis = amount > 0.0f ? b.center.*axis - reach : b.center.*axis + reach;
    blocked = true;
  }
  return blocked;
}

vec2 grid_cell(rng &r) {
  const f32 x = (f32)r.range(0, grid_size - 1) - (f32)grid_size / 2.0f;
  const f32 y = (f32)r.range(0, grid_size - 1) + 0.5f;
  return vec2{x, y} * cell_size;
}

// Gives every dead target a new cell: not its old one, not a live target's.
void place_targets(context &ctx) {
  for (target &t : g.targets) {
    if (!t.dead)
      continue;
    const vec2 old{t.pos.x, t.pos.y};
    vec2 cell = grid_cell(random(ctx));
    for (bool taken = true; taken;) {
      taken = cell == old;
      for (const target &o : g.targets)
        taken = taken || (!o.dead && o.pos.x == cell.x && o.pos.y == cell.y);
      if (taken)
        cell = grid_cell(random(ctx));
    }
    t = target{.pos = {cell.x, cell.y, target_z}, .dead = false};
  }
}

void shoot(context &ctx) {
  // The ray through the crosshair: the centre of the screen.
  const ray3d ray{.origin = g.pos, .direction = forward_of(g.yaw, g.pitch)};
  const vec3 dir = ray.direction;
  f32 nearest = 1e30f;
  target *hit_target = nullptr;
  bool hit = false;
  for (const box &b : level_boxes) {
    const ray3d_hit h = ray3d_box(ray, b.center, b.half * 2.0f);
    if (h.hit && h.distance < nearest) {
      nearest = h.distance;
      hit_target = nullptr;
      hit = true;
    }
  }
  for (target &tg : g.targets) {
    if (tg.dead)
      continue;
    const ray3d_hit h = ray3d_box(ray, tg.pos, vec3{target_half, target_half, target_half} * 2.0f);
    if (h.hit && h.distance < nearest) {
      nearest = h.distance;
      hit_target = &tg;
      hit = true;
    }
  }
  // Every shot kicks the view a little and lights the muzzle.
  const vec3 right = right_of(g.yaw);
  const vec3 forward = forward_of(g.yaw, g.pitch);
  const vec3 up = cross(right, forward);
  const vec3 muzzle = g.pos + right * muzzle_local.x + up * muzzle_local.y - forward * muzzle_local.z;
  camera_shake(ctx, 0.12f);
  g.muzzle = muzzle_time;
  g.muzzle_pos = muzzle;
  if (!hit)
    return;
  const vec3 end = g.pos + dir * nearest;
  gizmo_line3d(ctx, g.pos, end, colors::yellow, 1.0f);
  gizmo_point3d(ctx, end, colors::yellow, 1.0f);
  if (hit_target != nullptr)
    gizmo_text3d(ctx, end, "trúng", colors::red, 0.6f);
  particles3d_spawn(ctx, fx::sparks(), end, 18, {.direction = -dir, .scale = fx_scale});
  if (hit_target != nullptr) {
    g.ghosts.push_back({.pos = hit_target->pos, .seed = (f32)g.hits});
    particle_emitter burst = fx::explosion();
    burst.color_start = {1.0f, 0.45f, 0.35f, 1.0f};
    particles3d_spawn(ctx, burst, hit_target->pos, 40, {.scale = fx_scale * 0.5f});
    camera_shake(ctx, 0.25f);
    hitstop(ctx, 0.04f);
    hit_target->dead = true;
    g.hits++;
    place_targets(ctx);
  }
  g.tracers.push_back({.from = muzzle, .to = end, .lifetime = distance(muzzle, end) / tracer_speed});
}

void startup(context &ctx) {
  g = game_state{};
  g.gun = model_load(ctx, "assets/models/ak.glb");
  g.targets.resize(target_count, target{.pos = {0.0f, 0.0f, target_z}});
  place_targets(ctx);
  cursor_set_locked(ctx, true);
  gizmos_set_visible(ctx, false);
  // The tutorial's sun sits at (100, 200, 100), looking at the origin, and
  // casts shadows; the fog fades the far floor into the background.
  light3d_set(ctx, {.direction = {-100.0f, -200.0f, -100.0f},
                    .color = {0.8f, 0.8f, 0.8f, 1.0f},
                    .ambient = {0.3f, 0.3f, 0.34f, 1.0f},
                    .shadows = true,
                    .shadow_range = 90.0f,
                    .fog_color = fog_color,
                    .fog_density = 0.004f});
  // Bloom picks up what glows: tracers, lamps, the muzzle flash.
  post_fx_set(ctx, {.vignette = 0.3f, .bloom = 0.9f, .bloom_threshold = 0.9f});
  // The gun keeps the file's colours, made glossier.
  for (i32 i = 0; i < model_material_count(ctx, g.gun); i++) {
    model_material m = model_material_get(ctx, g.gun, i);
    m.surface.specular = 0.6f;
    m.surface.shininess = 48.0f;
    model_material_set(ctx, g.gun, i, m);
  }
}

void update(context &ctx) {
  if (key_pressed(ctx, key_f))
    g.flashlight = !g.flashlight;
  if (key_pressed(ctx, key_g))
    gizmos_set_visible(ctx, !gizmos_visible(ctx));
  for (const target &t : g.targets) {
    if (!t.dead)
      gizmo_box3d(ctx, t.pos, vec3{target_half, target_half, target_half} * 2.0f, colors::green);
  }
  if (key_pressed(ctx, key_escape)) {
    g.locked = !g.locked;
    cursor_set_locked(ctx, g.locked);
  }
  if (g.locked) {
    const vec2 d = mouse_delta(ctx);
    g.yaw -= d.x * sensitivity;
    g.pitch = clamp(g.pitch - d.y * sensitivity, -pitch_limit, pitch_limit);
    if (mouse_pressed(ctx, mouse_left))
      shoot(ctx);
  }
  const f32 dt = delta(ctx);
  for (tracer &t : g.tracers)
    t.age += dt;
  std::erase_if(g.tracers, [](const tracer &t) { return t.age > t.lifetime; });
  for (ghost &gh : g.ghosts)
    gh.age += dt;
  std::erase_if(g.ghosts, [](const ghost &gh) { return gh.age > ghost_time; });
  g.muzzle = std::max(0.0f, g.muzzle - dt);
}

void fixed_update(context &ctx) {
  const f32 dt = delta(ctx);
  // Standing on something cancels the fall, as the tutorial's controller does.
  if (g.grounded)
    g.velocity = {};
  vec2 input{};
  if (key_held(ctx, key_w))
    input.x += 1.0f;
  if (key_held(ctx, key_s))
    input.x -= 1.0f;
  if (key_held(ctx, key_d))
    input.y += 1.0f;
  if (key_held(ctx, key_a))
    input.y -= 1.0f;
  const vec3 flat_forward = forward_of(g.yaw, 0.0f);
  const vec3 move = normalize(flat_forward * input.x + right_of(g.yaw) * input.y);
  if (length_sq(move) > 0.0f) {
    g.velocity.x = move.x * move_speed;
    g.velocity.z = move.z * move_speed;
  }
  g.velocity.y -= gravity * dt;
  const std::vector<box> boxes = solids();
  move_axis(&vec3::x, g.velocity.x * dt, boxes);
  move_axis(&vec3::z, g.velocity.z * dt, boxes);
  g.grounded = move_axis(&vec3::y, g.velocity.y * dt, boxes) && g.velocity.y < 0.0f;
}

void render(context &ctx) {
  const vec3 forward = forward_of(g.yaw, g.pitch);
  begin_3d(ctx, {.position = g.pos, .target = g.pos + forward, .fovy = fov, .far_plane = 3000.0f});
  for (const lamp &l : lamps)
    light3d_add(ctx, {.position = l.pos, .color = l.color, .intensity = 1.6f, .radius = 45.0f});
  if (g.muzzle > 0.0f)
    light3d_add(ctx, {.position = g.muzzle_pos,
                      .color = {1.0f, 0.75f, 0.35f, 1.0f},
                      .intensity = 3.0f * g.muzzle / muzzle_time,
                      .radius = 30.0f});
  if (g.flashlight)
    light3d_add(ctx, {.kind = light3d_spot,
                      .position = g.pos,
                      .direction = forward,
                      .color = {1.0f, 0.95f, 0.85f, 1.0f},
                      .intensity = 2.0f,
                      .radius = 150.0f,
                      .cone = 32.0f,
                      .softness = 0.4f});

  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {2000.0f, 2000.0f}, floor_color);
  draw_cube3d(ctx, level_boxes[1].center, level_boxes[1].half * 2.0f, level_color);

  material3d_set(ctx, {.specular = 0.9f, .shininess = 64.0f, .emission = {1.0f, 0.1f, 0.05f, 0.25f}});
  for (const target &t : g.targets) {
    if (!t.dead)
      draw_sphere3d(ctx, t.pos, target_radius, target_color);
  }
  // A hit: white at once, then burning away.
  for (const ghost &gh : g.ghosts) {
    const f32 t = gh.age / ghost_time;
    fx3d_set(ctx, {.flash = {1.0f, 1.0f, 1.0f, std::max(0.0f, 1.0f - t * 3.0f)},
                   .dissolve = t,
                   .grain = 0.12f,
                   .seed = gh.seed});
    draw_sphere3d(ctx, gh.pos, target_radius * (1.0f + t * 0.4f), target_color);
  }
  fx3d_set(ctx, {});

  // What gives light shows no shading: lamp bulbs, tracers, the muzzle flash.
  for (const lamp &l : lamps) {
    material3d_set(ctx, {.emission = l.color, .unlit = true, .cast_shadows = false});
    draw_sphere3d(ctx, l.pos, 1.2f, l.color);
  }
  material3d_set(ctx, {.unlit = true, .cast_shadows = false});
  for (const tracer &t : g.tracers) {
    const vec3 dir = normalize(t.to - t.from);
    const vec3 at = lerp(t.from, t.to, clamp(t.age / t.lifetime, 0.0f, 1.0f));
    draw_cylinder3d(ctx, at - dir * 1.5f, at + dir * 1.5f, 0.06f, tracer_color);
  }
  if (g.muzzle > 0.0f)
    draw_sphere3d(ctx, g.muzzle_pos, 0.12f * g.muzzle / muzzle_time + 0.05f, {1.0f, 0.85f, 0.4f, 1.0f});
  material3d_set(ctx, {});
  // The gun hangs off the camera: same place, same rotation.
  draw_model(ctx, g.gun, {.position = g.pos, .rotation = {g.pitch, g.yaw, 0.0f}});
  end_3d(ctx);
}

void render_ui(context &ctx) {
  const vec2 screen = screen_size(ctx);
  constexpr f32 size = 2.0f;
  draw_rect(ctx, {{screen.x / 2.0f - size / 2.0f, screen.y / 2.0f - size / 2.0f}, {size, size}},
            crosshair_color);
  char line[64];
  std::snprintf(line, sizeof line, "Trúng: %d", g.hits);
  draw_text(ctx, line, {16.0f, 16.0f}, 22.0f, colors::white);
  if (!g.locked)
    draw_text(ctx, "Esc: khóa chuột để chơi", {16.0f, 44.0f}, 18.0f, colors::white);
  draw_text(ctx, "WASD: đi   Chuột trái: bắn   F: đèn pin   G: gizmo   Esc: nhả chuột", {16.0f, screen.y - 30.0f},
            16.0f, {0.8f, 0.82f, 0.88f, 1.0f});
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "fps_startup");
  ecs_register(ctx, phase_update, update, "fps_update");
  ecs_register(ctx, phase_fixed_update, fixed_update, "fps_move");
  ecs_register(ctx, phase_render, render, "fps_render");
  ecs_register(ctx, phase_post_render, render_ui, "fps_ui");
}
} // namespace

mod_desc fps_module() { return mod_desc{.name = "fps", .setup = setup}; }
} // namespace fps
