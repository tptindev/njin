#include "fps.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <vector>

// A port of Biped-Potato's Bevy FPS tutorial (part 2) to njin, with its gun
// model (assets/models/LICENSE.txt):
//   camera     mouse look with the cursor locked, pitch clamped
//   movement   WASD at a fixed step, gravity, box collision against the level
//   shooting   a ray from the eye through the crosshair (ray3d_box), nearest
//              hit wins
//   tracers    a streak from the muzzle to the hit point
//   targets    "grid shot": 5 targets on a 5x5 grid, a hit moves one
//   effects    shadows from the sun, fog, two coloured lamps and a flashlight
//              (F) that cast shadows too, a muzzle light, glowing tracers with bloom, sparks where
//              a shot lands, and a hit target flashes, bursts and dissolves
//              while the camera kicks
//   gizmos     G shows the debug view: target hit boxes, each shot's path for
//              a second, a label where it hit (njin_gizmo.h)
//   network    --host / --join (main.cpp): one player hosts and plays, others
//              join; everyone shoots at the same targets (njin_netcode.h).
//              The host moves every player from their inputs and owns the
//              targets and the score; a client moves its own player at once
//              (prediction) and draws the others interpolated.
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
bool move_axis(vec3 &pos, f32 vec3::*axis, f32 amount, const std::vector<box> &boxes) {
  pos.*axis += amount;
  bool blocked = false;
  for (const box &b : boxes) {
    if (!overlaps(pos, player_half, b))
      continue;
    const f32 reach = player_half.*axis + b.half.*axis;
    pos.*axis = amount > 0.0f ? b.center.*axis - reach : b.center.*axis + reach;
    blocked = true;
  }
  return blocked;
}

// One fixed step of a player: `input` is (forward, right), each -1..1. The
// same code moves the solo player, every player on the host, and the client's
// own prediction, so they agree.
void step_player(vec3 &pos, vec3 &velocity, bool &grounded, vec2 input, f32 yaw, f32 dt,
                 const std::vector<box> &boxes) {
  // Standing on something cancels the fall, as the tutorial's controller does.
  if (grounded)
    velocity = {};
  const vec3 move = normalize(forward_of(yaw, 0.0f) * input.x + right_of(yaw) * input.y);
  if (length_sq(move) > 0.0f) {
    velocity.x = move.x * move_speed;
    velocity.z = move.z * move_speed;
  }
  velocity.y -= gravity * dt;
  move_axis(pos, &vec3::x, velocity.x * dt, boxes);
  move_axis(pos, &vec3::z, velocity.z * dt, boxes);
  grounded = move_axis(pos, &vec3::y, velocity.y * dt, boxes) && velocity.y < 0.0f;
}

vec2 move_keys(const context &ctx) {
  vec2 input{};
  if (key_held(ctx, key_w))
    input.x += 1.0f;
  if (key_held(ctx, key_s))
    input.x -= 1.0f;
  if (key_held(ctx, key_d))
    input.y += 1.0f;
  if (key_held(ctx, key_a))
    input.y -= 1.0f;
  return input;
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

// Where a shot from `eye` along yaw/pitch lands: the nearest of the level and
// the live targets.
struct shot {
  vec3 muzzle;
  vec3 dir;
  vec3 end;
  bool hit = false;
  target *hit_target = nullptr;
};

vec3 muzzle_of(vec3 eye, f32 yaw, f32 pitch) {
  const vec3 right = right_of(yaw);
  const vec3 forward = forward_of(yaw, pitch);
  const vec3 up = cross(right, forward);
  return eye + right * muzzle_local.x + up * muzzle_local.y - forward * muzzle_local.z;
}

shot trace_shot(vec3 eye, f32 yaw, f32 pitch) {
  // The ray through the crosshair: the centre of the screen.
  const ray3d ray{.origin = eye, .direction = forward_of(yaw, pitch)};
  shot out;
  out.dir = ray.direction;
  out.muzzle = muzzle_of(eye, yaw, pitch);
  f32 nearest = 1e30f;
  for (const box &b : level_boxes) {
    const ray3d_hit h = ray3d_box(ray, b.center, b.half * 2.0f);
    if (h.hit && h.distance < nearest) {
      nearest = h.distance;
      out.hit_target = nullptr;
      out.hit = true;
    }
  }
  for (target &tg : g.targets) {
    if (tg.dead)
      continue;
    const ray3d_hit h = ray3d_box(ray, tg.pos, vec3{target_half, target_half, target_half} * 2.0f);
    if (h.hit && h.distance < nearest) {
      nearest = h.distance;
      out.hit_target = &tg;
      out.hit = true;
    }
  }
  out.end = out.hit ? eye + out.dir * nearest : eye + out.dir * 1000.0f;
  return out;
}

// The look of a shot: our own kicks the view and lights the muzzle; anyone's
// leaves sparks where it lands and a tracer.
void shot_effects(context &ctx, vec3 eye, const shot &s, bool own) {
  if (own) {
    camera_shake(ctx, 0.12f);
    g.muzzle = muzzle_time;
    g.muzzle_pos = s.muzzle;
  }
  if (!s.hit)
    return;
  gizmo_line3d(ctx, eye, s.end, colors::yellow, 1.0f);
  gizmo_point3d(ctx, s.end, colors::yellow, 1.0f);
  particles3d_spawn(ctx, fx::sparks(), s.end, 18, {.direction = -s.dir, .scale = fx_scale});
  g.tracers.push_back({.from = s.muzzle, .to = s.end, .lifetime = distance(s.muzzle, s.end) / tracer_speed});
}

// A target hit at `pos` flashes, bursts and dissolves there. `own` kicks the
// camera; `stop` also freezes time a moment (solo only: online it would hold
// up the host's ticks).
void target_hit_effects(context &ctx, vec3 pos, vec3 at, f32 seed, bool own, bool stop) {
  gizmo_text3d(ctx, at, "trúng", colors::red, 0.6f);
  g.ghosts.push_back({.pos = pos, .seed = seed});
  particle_emitter burst = fx::explosion();
  burst.color_start = {1.0f, 0.45f, 0.35f, 1.0f};
  particles3d_spawn(ctx, burst, pos, 40, {.scale = fx_scale * 0.5f});
  if (own)
    camera_shake(ctx, 0.25f);
  if (stop)
    hitstop(ctx, 0.04f);
}

void shoot(context &ctx) {
  const shot s = trace_shot(g.pos, g.yaw, g.pitch);
  shot_effects(ctx, g.pos, s, true);
  if (s.hit_target == nullptr)
    return;
  target_hit_effects(ctx, s.hit_target->pos, s.end, (f32)g.hits, true, true);
  s.hit_target->dead = true;
  g.hits++;
  place_targets(ctx);
}


// ---------------------------------------------------------------- network --
// Replicated components. Positions to 1 cm, angles to about 0.005 degrees:
// a body is under 10 bytes.
struct mp_body {
  vec3 pos = spawn;
  f32 yaw = 0.0f;   // degrees
  f32 pitch = 0.0f; // degrees
};
struct mp_player {
  i32 id = 0; // netcode player id; 0 is the host
};
struct mp_score {
  i32 hits = 0;
};
struct mp_target {
  vec3 pos;
};
// Fall speed and footing: only the owner reads it, to replay from the host's
// state (velocity sideways comes from the input every tick).
struct mp_motion {
  f32 fall = 0.0f;
  bool grounded = false;
};

constexpr f32 world_extent = 2048.0f;

void write_body(const mp_body &b, bit_writer &w) {
  w.write_vec3(b.pos, -world_extent, world_extent, 0.01f);
  w.write_angle(b.yaw * (pi / 180.0f), 16);
  w.write_float(b.pitch, -90.0f, 90.0f, 0.01f);
}
void read_body(mp_body &b, bit_reader &r) {
  b.pos = r.read_vec3(-world_extent, world_extent, 0.01f);
  b.yaw = r.read_angle(16) * (180.0f / pi);
  b.pitch = r.read_float(-90.0f, 90.0f, 0.01f);
}
void read_motion(mp_motion &m, bit_reader &r) {
  m.fall = r.read_float(-200.0f, 200.0f, 0.001f);
  m.grounded = r.read_bool();
}
mp_body lerp_body(const mp_body &a, const mp_body &b, f32 t) {
  f32 turn = std::fmod(b.yaw - a.yaw + 540.0f, 360.0f) - 180.0f; // the short way round
  return {lerp(a.pos, b.pos, t), a.yaw + turn * t, a.pitch + (b.pitch - a.pitch) * t};
}

// What a player sends each tick: the keys held, where it looks, whether it
// fired since the last tick.
struct mp_input {
  vec2 move{};
  f32 yaw = 0.0f;
  f32 pitch = 0.0f;
  bool fire = false;
};

std::string encode_input(const mp_input &in) {
  bit_writer w;
  w.write_int((i32)in.move.x, -1, 1);
  w.write_int((i32)in.move.y, -1, 1);
  w.write_angle(in.yaw * (pi / 180.0f), 16);
  w.write_float(in.pitch, -90.0f, 90.0f, 0.01f);
  w.write_bool(in.fire);
  return w.data();
}

bool decode_input(const std::string &bytes, mp_input &out) {
  bit_reader r(bytes);
  out.move = {(f32)r.read_int(-1, 1), (f32)r.read_int(-1, 1)};
  out.yaw = r.read_angle(16) * (180.0f / pi);
  out.pitch = r.read_float(-90.0f, 90.0f, 0.01f);
  out.fire = r.read_bool();
  return r.ok();
}

// A shot, sent by the host to everyone so all see it (and who hit what).
struct mp_shot_msg {
  i32 shooter = 0;
  vec3 eye;
  f32 yaw = 0.0f;
  f32 pitch = 0.0f;
  bool hit = false;
  vec3 end;
  bool hit_target = false;
  vec3 target_pos;
};

std::string encode_shot(const mp_shot_msg &m) {
  bit_writer w;
  w.write_varint((u64)m.shooter);
  w.write_vec3(m.eye, -world_extent, world_extent, 0.01f);
  w.write_angle(m.yaw * (pi / 180.0f), 16);
  w.write_float(m.pitch, -90.0f, 90.0f, 0.01f);
  w.write_bool(m.hit);
  w.write_vec3(m.end, -world_extent, world_extent, 0.01f);
  w.write_bool(m.hit_target);
  w.write_vec3(m.target_pos, -world_extent, world_extent, 0.01f);
  return w.data();
}

bool decode_shot(const std::string &bytes, mp_shot_msg &m) {
  bit_reader r(bytes);
  m.shooter = (i32)r.read_varint();
  m.eye = r.read_vec3(-world_extent, world_extent, 0.01f);
  m.yaw = r.read_angle(16) * (180.0f / pi);
  m.pitch = r.read_float(-90.0f, 90.0f, 0.01f);
  m.hit = r.read_bool();
  m.end = r.read_vec3(-world_extent, world_extent, 0.01f);
  m.hit_target = r.read_bool();
  m.target_pos = r.read_vec3(-world_extent, world_extent, 0.01f);
  return r.ok();
}

struct mover {
  vec3 velocity{};
  bool grounded = false;
};

// One tick of the client's own player: the input it ran and where it ended.
struct predicted {
  mp_input input;
  vec3 pos;
  mover move;
};

struct mp_state {
  net_mode mode;
  bool fire = false; // clicked since the last tick
  std::string status;
  // host
  std::unordered_map<u32, entt::entity> players; // player id (0: the host) -> entity
  std::unordered_map<u32, mover> movers;
  std::vector<entt::entity> targets;
  // client
  entt::entity me = entt::null;
  mover my_move;
  std::map<u32, predicted> history; // each tick the client moved its own player
  u32 checked = 0;             // newest server tick compared with the prediction
  f32 correction = 0.0f;       // size of the last correction, for the HUD
  f32 worst_correction = 0.0f; // since the last log line
  f32 bot_fire = 0.0f;
  f32 log_timer = 0.0f;
};
mp_state mp;

bool online() { return mp.mode.kind != net_mode::solo; }

rgba player_color(i32 id) {
  static const rgba colors_by_id[] = {{0.3f, 0.8f, 1.0f, 1.0f}, {1.0f, 0.5f, 0.2f, 1.0f}, {0.4f, 1.0f, 0.4f, 1.0f},
                                      {1.0f, 0.35f, 0.8f, 1.0f}, {1.0f, 0.9f, 0.3f, 1.0f}, {0.7f, 0.6f, 1.0f, 1.0f}};
  return colors_by_id[(u32)id % 6];
}

void register_net_components(context &ctx) {
  // Same order on every machine.
  netcode_component<mp_body>(ctx, {.write = write_body, .read = read_body, .lerp = lerp_body, .predicted = true});
  netcode_component<mp_player>(ctx, {
                                        .write = [](const mp_player &p, bit_writer &w) { w.write_varint((u64)p.id); },
                                        .read = [](mp_player &p, bit_reader &r) { p.id = (i32)r.read_varint(); },
                                    });
  netcode_component<mp_score>(ctx, {
                                       .write = [](const mp_score &sc, bit_writer &w) { w.write_varint((u64)sc.hits); },
                                       .read = [](mp_score &sc, bit_reader &r) { sc.hits = (i32)r.read_varint(); },
                                   });
  netcode_component<mp_motion>(ctx, {
                                        .write = [](const mp_motion &m, bit_writer &w) {
                                          w.write_float(m.fall, -200.0f, 200.0f, 0.001f);
                                          w.write_bool(m.grounded);
                                        },
                                        .read = [](mp_motion &m, bit_reader &r) {
                                          m.fall = r.read_float(-200.0f, 200.0f, 0.001f);
                                          m.grounded = r.read_bool();
                                        },
                                        .predicted = true,
                                    });
  netcode_component<mp_target>(ctx, {
                                        .write = [](const mp_target &t,
                                                    bit_writer &w) { w.write_vec3(t.pos, -world_extent, world_extent, 0.01f); },
                                        .read = [](mp_target &t,
                                                   bit_reader &r) { t.pos = r.read_vec3(-world_extent, world_extent, 0.01f); },
                                    });
}

// --bot: strafes left and right and sweeps its aim across the targets, so two
// windows can play without anyone at the keyboard.
vec2 bot_move(const context &ctx) { return {0.0f, std::sin(elapsed(ctx) * 1.5f) >= 0.0f ? 1.0f : -1.0f}; }

mp_input local_input(const context &ctx) {
  const vec2 move = mp.mode.bot ? bot_move(ctx) : g.locked ? move_keys(ctx) : vec2{};
  return {.move = move, .yaw = g.yaw, .pitch = g.pitch, .fire = mp.fire};
}

// --- host ---

entt::entity add_player(context &ctx, u32 id) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  // Side by side, so nobody spawns inside anyone.
  reg.emplace<mp_body>(e, mp_body{.pos = spawn + vec3{(f32)((i32)(id % 8) - 4) * 6.0f, 0.0f, 0.0f}});
  reg.emplace<mp_player>(e, (i32)id);
  reg.emplace<mp_score>(e);
  reg.emplace<mp_motion>(e);
  netcode_replicate(ctx, e, id); // the host's own (id 0) has no owner: clients interpolate it
  mp.players[id] = e;
  mp.movers[id] = {};
  return e;
}

void host_shot(context &ctx, u32 shooter, const mp_body &body) {
  const shot s = trace_shot(body.pos, body.yaw, body.pitch);
  mp_shot_msg m;
  m.shooter = (i32)shooter;
  m.eye = body.pos;
  m.yaw = body.yaw;
  m.pitch = body.pitch;
  m.hit = s.hit;
  m.end = s.end;
  if (s.hit_target != nullptr) {
    m.hit_target = true;
    m.target_pos = s.hit_target->pos;
    s.hit_target->dead = true;
    place_targets(ctx);
    world(ctx).get<mp_score>(mp.players[shooter]).hits++;
  }
  netcode_send_all(ctx, encode_shot(m), net_delivery::reliable);
  // The host sees it at once (its own muzzle flashed on the click).
  const bool own = shooter == 0;
  shot_effects(ctx, m.eye, s, false);
  if (m.hit_target)
    target_hit_effects(ctx, m.target_pos, m.end, (f32)world(ctx).get<mp_score>(mp.players[shooter]).hits, own, false);
}

void host_fixed(context &ctx) {
  entt::registry &reg = world(ctx);
  netcode_set_input(ctx, encode_input(local_input(ctx)));
  mp.fire = false;
  const std::vector<box> boxes = solids();
  for (auto &[id, e] : mp.players) {
    std::string bytes;
    const bool fresh = netcode_input(ctx, id, bytes);
    mp_body &body = reg.get<mp_body>(e);
    mp_input in{.yaw = body.yaw, .pitch = body.pitch};
    if (!bytes.empty() && !decode_input(bytes, in))
      in = mp_input{.yaw = body.yaw, .pitch = body.pitch}; // damaged: stand still
    mover &mv = mp.movers[id];
    // Quantized like the replicated fall speed, so the client replays the same.
    mv.velocity.y = std::round(mv.velocity.y * 1000.0f) / 1000.0f;
    step_player(body.pos, mv.velocity, mv.grounded, in.move, in.yaw, delta(ctx), boxes);
    body.pos = vec3{std::round(body.pos.x * 100.0f), std::round(body.pos.y * 100.0f), std::round(body.pos.z * 100.0f)} / 100.0f;
    body.yaw = in.yaw;
    body.pitch = in.pitch;
    reg.get<mp_motion>(e) = {.fall = mv.velocity.y, .grounded = mv.grounded};
    // A repeated input (the real one is late) never fires twice.
    if (fresh && in.fire)
      host_shot(ctx, id, body);
  }
  for (usize i = 0; i < mp.targets.size(); i++)
    reg.get<mp_target>(mp.targets[i]).pos = g.targets[i].pos;
  g.pos = reg.get<mp_body>(mp.players[0]).pos;
}

void host_update(context &ctx) {
  netcode_event e;
  while (netcode_poll(ctx, e)) {
    if (e.kind == netcode_event_kind::joined) {
      add_player(ctx, e.player);
    } else if (e.kind == netcode_event_kind::left) {
      const auto it = mp.players.find(e.player);
      if (it != mp.players.end()) {
        world(ctx).destroy(it->second);
        mp.players.erase(it);
        mp.movers.erase(e.player);
      }
    }
  }
}

void host_start(context &ctx) {
  netcode_opts opts;
  opts.sim = mp.mode.sim;
  if (!netcode_host(ctx, mp.mode.port, opts)) {
    mp.status = "Không mở được cổng " + std::to_string(mp.mode.port) + "; đang chơi một mình";
    mp.mode.kind = net_mode::solo;
    return;
  }
  add_player(ctx, 0);
  entt::registry &reg = world(ctx);
  for (const target &t : g.targets) {
    const entt::entity e = reg.create();
    reg.emplace<mp_target>(e, t.pos);
    netcode_replicate(ctx, e);
    mp.targets.push_back(e);
  }
  mp.status = "Đang mở phòng ở cổng " + std::to_string(mp.mode.port);
}

// --- client ---

void client_fixed(context &ctx) {
  if (!netcode_synced(ctx))
    return;
  netcode_set_input(ctx, encode_input(local_input(ctx)));
  mp.fire = false;
  entt::registry &reg = world(ctx);
  if (mp.me == entt::null || !reg.valid(mp.me))
    return; // our player has not arrived yet
  // Move at once with the same input the host will use (quantized the same).
  std::string bytes;
  netcode_input(ctx, 0, bytes);
  mp_input in;
  if (bytes.empty() || !decode_input(bytes, in))
    return;
  mp_body &body = reg.get<mp_body>(mp.me);
  const std::vector<box> boxes = solids();
  // The same steps as the host, rounded the same way.
  const auto step = [&](vec3 &pos, mover &mv, const mp_input &input) {
    mv.velocity.y = std::round(mv.velocity.y * 1000.0f) / 1000.0f;
    step_player(pos, mv.velocity, mv.grounded, input.move, input.yaw, delta(ctx), boxes);
    pos = vec3{std::round(pos.x * 100.0f), std::round(pos.y * 100.0f), std::round(pos.z * 100.0f)} / 100.0f;
  };
  // Where the host says we were at an older tick, against where we said. If
  // they differ (pushed by a target, a late input), start again from the
  // host's state at that tick and replay our inputs since.
  mp_body confirmed;
  mp_motion motion;
  u32 at = 0, at_motion = 0;
  if (netcode_confirmed(ctx, mp.me, confirmed, at, read_body) &&
      netcode_confirmed(ctx, mp.me, motion, at_motion, read_motion) && at == at_motion && at > mp.checked) {
    mp.checked = at;
    const auto h = mp.history.find(at);
    if (h != mp.history.end() && length(confirmed.pos - h->second.pos) > 0.02f) {
      mp.correction = length(confirmed.pos - h->second.pos);
      mp.worst_correction = std::max(mp.worst_correction, mp.correction);
      vec3 pos = confirmed.pos;
      mover mv{.velocity = {0.0f, motion.fall, 0.0f}, .grounded = motion.grounded};
      h->second.pos = pos;
      h->second.move = mv;
      for (auto it = std::next(h); it != mp.history.end(); ++it) {
        step(pos, mv, it->second.input);
        it->second.pos = pos;
        it->second.move = mv;
      }
      body.pos = pos;
      mp.my_move = mv;
    }
    mp.history.erase(mp.history.begin(), mp.history.lower_bound(at));
  }
  step(body.pos, mp.my_move, in);
  body.yaw = in.yaw;
  body.pitch = in.pitch;
  mp.history[netcode_tick(ctx)] = {in, body.pos, mp.my_move};
  if (mp.history.size() > 256)
    mp.history.erase(mp.history.begin());
}

void client_update(context &ctx) {
  entt::registry &reg = world(ctx);
  netcode_event e;
  while (netcode_poll(ctx, e)) {
    if (e.kind == netcode_event_kind::joined) {
      mp.status = "Đã vào phòng, bạn là người chơi " + std::to_string(e.player);
    } else if (e.kind == netcode_event_kind::left) {
      mp.status = e.code == netcode_code_full      ? "Phòng đã đủ người"
                  : e.code == netcode_code_version ? "Khác phiên bản với chủ phòng"
                                                   : "Mất kết nối với chủ phòng";
      mp.me = entt::null;
    } else if (e.kind == netcode_event_kind::message) {
      mp_shot_msg m;
      if (!decode_shot(e.data, m))
        continue;
      const bool own = (u32)m.shooter == netcode_self(ctx);
      // Our own shot was drawn when we clicked; the others' are drawn now.
      if (!own) {
        shot s{.muzzle = muzzle_of(m.eye, m.yaw, m.pitch), .dir = forward_of(m.yaw, m.pitch), .end = m.end, .hit = m.hit};
        shot_effects(ctx, m.eye, s, false);
      }
      if (m.hit_target)
        target_hit_effects(ctx, m.target_pos, m.end, m.target_pos.x + m.target_pos.y, own, false);
    }
  }
  // Our player, once it has arrived.
  if (mp.me == entt::null || !reg.valid(mp.me))
    for (auto [en, body] : reg.view<mp_body>().each())
      if (netcode_is_mine(ctx, en))
        mp.me = en;
  if (mp.me != entt::null && reg.valid(mp.me))
    g.pos = reg.get<mp_body>(mp.me).pos;
  // The targets are the host's.
  g.targets.clear();
  for (auto [en, t] : reg.view<mp_target>().each())
    g.targets.push_back({.pos = t.pos, .dead = false});
}

void client_start(context &ctx) {
  netcode_opts opts;
  opts.sim = mp.mode.sim;
  g.targets.clear(); // the host's arrive with the first snapshot
  if (!netcode_join(ctx, mp.mode.address.c_str(), mp.mode.port, opts)) {
    mp.status = "Địa chỉ không hợp lệ: " + mp.mode.address;
    return;
  }
  mp.status = "Đang vào phòng " + mp.mode.address + ":" + std::to_string(mp.mode.port) + "...";
}

// A click online: the host shoots in its next tick; a client draws its shot at
// once and lets the host decide what it hit.
void click_online(context &ctx) {
  mp.fire = true;
  if (mp.mode.kind == net_mode::join) {
    shot_effects(ctx, g.pos, trace_shot(g.pos, g.yaw, g.pitch), true);
  } else {
    camera_shake(ctx, 0.12f);
    g.muzzle = muzzle_time;
    g.muzzle_pos = muzzle_of(g.pos, g.yaw, g.pitch);
  }
}

// Online and every 5 s: one line in the log (what a test harness reads).
void log_online(context &ctx) {
  mp.log_timer += delta_real(ctx);
  if (mp.log_timer < 5.0f)
    return;
  mp.log_timer = 0.0f;
  entt::registry &reg = world(ctx);
  i32 players = 0, mine = -1;
  for (auto [en, who, score] : reg.view<mp_player, mp_score>().each()) {
    players++;
    if (mp.mode.kind == net_mode::host ? who.id == 0 : en == mp.me)
      mine = score.hits;
  }
  const netcode_stats st = netcode_info(ctx);
  NJIN_INFO("fps net: %s players=%d score=%d tick=%u rtt=%.0fms margin=%d rate=%.3f worst_correction=%.2f "
            "snapshot=%u late_inputs=%u",
            mp.mode.kind == net_mode::host ? "host" : "client", players, mine, st.tick, st.rtt * 1000.0f,
            st.input_margin, st.fixed_rate, mp.worst_correction, st.snapshot_bytes, st.missing_inputs);
  mp.worst_correction = 0.0f;
}

void draw_players(context &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [en, body, who] : reg.view<mp_body, mp_player>().each()) {
    const bool self = mp.mode.kind == net_mode::host ? who.id == 0 : en == mp.me;
    if (self)
      continue;
    // A body down to the floor, a head at the eye with a visor where it looks,
    // and the gun where its camera would hold it.
    const vec3 forward = forward_of(body.yaw, body.pitch);
    draw_capsule3d(ctx, body.pos - vec3{0.0f, player_half.y - 2.0f, 0.0f}, body.pos - vec3{0.0f, 3.0f, 0.0f}, 2.0f,
                   player_color(who.id));
    draw_sphere3d(ctx, body.pos, 1.5f, player_color(who.id));
    draw_sphere3d(ctx, body.pos + forward * 1.2f, 0.6f, colors::white);
    draw_model(ctx, g.gun, {.position = body.pos, .rotation = {body.pitch, body.yaw, 0.0f}});
  }
}

void draw_online_ui(context &ctx) {
  entt::registry &reg = world(ctx);
  const vec2 screen = screen_size(ctx);
  f32 y = 44.0f;
  draw_text(ctx, mp.status.c_str(), {16.0f, y}, 18.0f, {0.8f, 0.9f, 1.0f, 1.0f});
  y += 26.0f;
  const netcode_stats st = netcode_info(ctx);
  char line[160];
  if (mp.mode.kind == net_mode::join)
    std::snprintf(line, sizeof line, "ping %.0f ms  tick %u  input sớm %d tick  nhịp %.3f  sửa lệch %.2f", st.rtt * 1000.0f,
                  st.tick, st.input_margin, st.fixed_rate, mp.correction);
  else
    std::snprintf(line, sizeof line, "tick %u  snapshot %u byte  input trễ %u lần", st.tick, st.snapshot_bytes,
                  st.missing_inputs);
  draw_text(ctx, line, {16.0f, y}, 16.0f, {0.75f, 0.78f, 0.85f, 1.0f});
  // The scoreboard, top right.
  f32 row = 16.0f;
  for (auto [en, who, score] : reg.view<mp_player, mp_score>().each()) {
    const bool self = mp.mode.kind == net_mode::host ? who.id == 0 : en == mp.me;
    std::string name = who.id == 0 ? "Chủ phòng" : "Người chơi " + std::to_string(who.id);
    if (self)
      name += " (bạn)";
    std::snprintf(line, sizeof line, "%s: %d", name.c_str(), score.hits);
    draw_text(ctx, line, {screen.x - 280.0f, row}, 18.0f, player_color(who.id));
    row += 24.0f;
  }
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
  if (mp.mode.kind == net_mode::host)
    host_start(ctx);
  else if (mp.mode.kind == net_mode::join)
    client_start(ctx);
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
  if (online() && mp.mode.bot) {
    // Aims at one target after another, then fires.
    mp.bot_fire += delta_real(ctx);
    if (!g.targets.empty()) {
      const vec3 d = g.targets[(usize)(elapsed(ctx) / 0.5f) % g.targets.size()].pos - g.pos;
      g.yaw = std::atan2(-d.x, -d.z) * (180.0f / pi);
      g.pitch = std::asin(d.y / length(d)) * (180.0f / pi);
      if (mp.bot_fire > 0.5f) {
        mp.bot_fire = 0.0f;
        click_online(ctx);
      }
    }
  } else if (g.locked) {
    const vec2 d = mouse_delta(ctx);
    g.yaw -= d.x * sensitivity;
    g.pitch = clamp(g.pitch - d.y * sensitivity, -pitch_limit, pitch_limit);
    if (mouse_pressed(ctx, mouse_left)) {
      if (online())
        click_online(ctx);
      else
        shoot(ctx);
    }
  }
  const f32 dt = delta(ctx);
  for (tracer &t : g.tracers)
    t.age += dt;
  std::erase_if(g.tracers, [](const tracer &t) { return t.age > t.lifetime; });
  for (ghost &gh : g.ghosts)
    gh.age += dt;
  std::erase_if(g.ghosts, [](const ghost &gh) { return gh.age > ghost_time; });
  g.muzzle = std::max(0.0f, g.muzzle - dt);
  if (online())
    log_online(ctx);
}

void fixed_update(context &ctx) {
  step_player(g.pos, g.velocity, g.grounded, move_keys(ctx), g.yaw, delta(ctx), solids());
}

void render(context &ctx) {
  const vec3 forward = forward_of(g.yaw, g.pitch);
  begin_3d(ctx, {.position = g.pos, .target = g.pos + forward, .fovy = fov, .far_plane = 3000.0f});
  for (const lamp &l : lamps)
    light3d_add(ctx, {.position = l.pos, .color = l.color, .intensity = 1.6f, .radius = 45.0f, .shadows = true});
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
                      .softness = 0.4f,
                      .shadows = true});

  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {2000.0f, 2000.0f}, floor_color);
  draw_cube3d(ctx, level_boxes[1].center, level_boxes[1].half * 2.0f, level_color);
  if (online())
    draw_players(ctx);

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
  if (online())
    draw_online_ui(ctx);
  else
    draw_text(ctx, line, {16.0f, 16.0f}, 22.0f, colors::white);
  if (!g.locked)
    draw_text(ctx, "Esc: khóa chuột để chơi", {16.0f, 44.0f}, 18.0f, colors::white);
  draw_text(ctx, "WASD: đi   Chuột trái: bắn   F: đèn pin   G: gizmo   Esc: nhả chuột", {16.0f, screen.y - 30.0f},
            16.0f, {0.8f, 0.82f, 0.88f, 1.0f});
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "fps_startup");
  ecs_register(ctx, phase_update, update, "fps_update");
  if (mp.mode.kind == net_mode::host) {
    register_net_components(ctx);
    ecs_register(ctx, phase_update, host_update, "fps_host");
    ecs_register(ctx, phase_fixed_update, host_fixed, "fps_host_tick");
  } else if (mp.mode.kind == net_mode::join) {
    register_net_components(ctx);
    ecs_register(ctx, phase_update, client_update, "fps_client");
    ecs_register(ctx, phase_fixed_update, client_fixed, "fps_client_tick");
  } else {
    ecs_register(ctx, phase_fixed_update, fixed_update, "fps_move");
  }
  ecs_register(ctx, phase_render, render, "fps_render");
  ecs_register(ctx, phase_post_render, render_ui, "fps_ui");
}
} // namespace

mod_desc fps_module() { return fps_module(net_mode{}); }

mod_desc fps_module(const net_mode &mode) {
  mp = mp_state{};
  mp.mode = mode;
  return mod_desc{.name = "fps", .setup = setup};
}
} // namespace fps
