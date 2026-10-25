#include "platformer3d.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// A 3D platformer on njin's 3D API (njin_3d.h) and 3D physics
// (njin_physics3d.h): a third-person camera, an animated robot, and a run of
// floating platforms up to a goal.
//   camera     mouse-look orbit behind the player; movement is camera-relative,
//              like a typical third-person platformer. A raycast pulls the
//              camera in when a platform is between it and the player
//   player     an entity: a character3d (the physics capsule, which moves the
//              entity's transform3d) and a model3d, a glTF robot with idle, run
//              and jump clips; idle and run blend by speed
//   physics    the platforms are static bodies, one platform is kinematic and
//              slides back and forth, the crates on the start platform are
//              dynamic entities (push them off), the hill next to the start is
//              a triangle-mesh body from its model, and the seesaw beside it is
//              a plank on a hinge joint
//   movement   the game drives the character by velocity: acceleration toward
//              the input, gravity, a jump with coyote time and a jump buffer
//              (the feel of njin::platformer_body in 2D), one extra air jump,
//              and the velocity of the platform underfoot so it carries you
//   pickups    coins are sensor bodies: touching one is a contact event, which
//              destroys the coin entity (and its body with it)
//   checkpoint falling off respawns on the highest platform reached, with a
//              dissolve out and in
//   goal       a spinning SDF torus around a sensor; touching it bursts
//              particles, flashes the screen, then restarts the run
//   gizmos     G shows the debug view: platform boxes, checkpoint, velocity

namespace platformer3d {
namespace {
using namespace njin;

constexpr f32 gravity = 26.0f;
constexpr f32 move_speed = 7.0f;
constexpr f32 accel = 40.0f; // ground: reaches move_speed in move_speed/accel seconds
constexpr f32 air_accel = 18.0f;
constexpr f32 jump_speed = 10.5f;
constexpr f32 coyote_time = 0.12f;
constexpr f32 jump_buffer = 0.12f;
constexpr f32 turn_speed = 900.0f; // degrees per second the model turns to face its movement
constexpr f32 player_radius = 0.33f;
constexpr f32 player_height = 1.12f;
constexpr f32 fall_y = -12.0f; // below this, respawn at the checkpoint

constexpr f32 cam_sensitivity = 0.25f;
constexpr f32 cam_distance = 6.5f;
constexpr f32 cam_pitch_min = -25.0f, cam_pitch_max = 65.0f;

constexpr f32 fx_scale = 0.02f; // njin::fx emitter pixels to world units

constexpr rgba sky_color{0.55f, 0.68f, 0.85f, 1.0f};
constexpr rgba platform_color{0.55f, 0.58f, 0.66f, 1.0f};
constexpr rgba platform_top_color{0.68f, 0.72f, 0.8f, 1.0f};
constexpr rgba moving_color{0.75f, 0.45f, 0.4f, 1.0f};
constexpr rgba crate_color{0.78f, 0.55f, 0.3f, 1.0f};
constexpr rgba plank_color{0.62f, 0.46f, 0.32f, 1.0f};
constexpr rgba coin_color{1.0f, 0.82f, 0.25f, 1.0f};
constexpr rgba goal_color{0.3f, 0.9f, 0.6f, 1.0f};

constexpr vec3 start_pos{0.0f, 0.5f, 1.5f};
constexpr vec3 goal_pos{0.0f, 8.6f, -54.0f};
constexpr f32 goal_radius = 1.4f;
constexpr f32 respawn_out = 0.35f, respawn_in = 0.35f;
constexpr vec3 crate_size{0.9f, 0.9f, 0.9f};
constexpr vec3 hill_pos{7.2f, 0.0f, 0.0f};
constexpr vec3 seesaw_pivot{-5.4f, 0.9f, 0.0f};
constexpr vec3 plank_size{4.0f, 0.15f, 1.4f};

struct platform {
  vec3 center;
  vec3 half;
  body3d_handle body;
};

// Tags for the game's own entities.
struct crate {};
struct coin {};

struct game_state {
  character3d_handle player;
  entt::entity hero = entt::null; // the player's entity: character3d, model3d
  model_handle robot{};
  model_handle hill{};
  i32 idle = -1, run = -1, jump = -1; // the robot's clips
  vec3 move_vel{};   // the player's own horizontal velocity
  f32 vertical = 0.0f;
  bool grounded = false;
  f32 coyote = 0.0f;
  f32 jump_want = 0.0f;
  bool air_jump_used = false;
  f32 facing_yaw = 0.0f; // degrees, the model's own facing

  f32 cam_yaw = 180.0f, cam_pitch = 15.0f;
  bool locked = true;

  std::vector<platform> platforms;
  platform mover;
  vec3 mover_a{-2.0f, 5.4f, -41.0f}, mover_b{2.0f, 5.4f, -41.5f};
  f32 mover_period = 3.0f;
  f32 clock = 0.0f; // fixed-step time, drives the mover
  body3d_handle goal;
  joint3d_handle seesaw;

  i32 checkpoint = -1; // index into platforms, -1 is the start
  vec3 checkpoint_pos = start_pos;

  bool respawning = false;
  f32 respawn_t = 0.0f;
  bool won = false;
  f32 win_t = 0.0f;
  i32 jumps = 0;
  i32 coins = 0;
};
game_state g;

// The staircase up to the goal, with gaps that need a running jump and one
// that needs the air jump. The moving platform crosses the gap after index 6.
const platform platform_layout[] = {
    {{0.0f, 0.0f, 0.0f}, {3.0f, 0.5f, 3.0f}, {}}, // start
    {{0.0f, 0.6f, -7.0f}, {1.6f, 0.5f, 1.6f}, {}},
    {{2.2f, 1.4f, -12.5f}, {1.4f, 0.5f, 1.4f}, {}},
    {{0.5f, 2.4f, -18.0f}, {1.3f, 0.5f, 1.3f}, {}},
    {{0.5f, 3.0f, -25.5f}, {1.3f, 0.5f, 1.3f}, {}}, // needs the air jump
    {{-2.0f, 3.6f, -30.5f}, {1.2f, 0.5f, 1.2f}, {}},
    {{-2.0f, 4.4f, -36.0f}, {1.4f, 0.5f, 1.4f}, {}},
    {{0.0f, 6.4f, -46.5f}, {1.6f, 0.5f, 1.6f}, {}},
    {{0.0f, 7.4f, -51.0f}, {1.5f, 0.5f, 1.5f}, {}},
    {{goal_pos.x, goal_pos.y - 1.1f, goal_pos.z}, {2.5f, 0.5f, 2.5f}, {}}, // goal
};

// Where the coins float: over the hill, the far end of the seesaw, and
// along the climb.
const vec3 coin_spots[] = {{7.2f, 2.5f, 0.0f},    {-7.0f, 1.9f, 0.0f},   {2.2f, 2.7f, -12.5f},
                           {0.5f, 4.3f, -25.5f},  {-2.0f, 5.7f, -36.0f}, {0.0f, 7.7f, -46.5f}};

vec3 flat(vec3 v) {
  v.y = 0.0f;
  const f32 len = length(v);
  return len > 1e-5f ? v / len : vec3{0.0f, 0.0f, -1.0f};
}

vec3 cam_forward() {
  const f32 y = g.cam_yaw * (pi / 180.0f), p = g.cam_pitch * (pi / 180.0f);
  return {std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
}

vec3 player_pos(const context &ctx) { return character3d_position(ctx, g.player); }

vec3 mover_at(f32 t) {
  const f32 k = (std::sin(t * (2.0f * pi / g.mover_period)) + 1.0f) * 0.5f;
  return lerp(g.mover_a, g.mover_b, k);
}

// Crates back on the start platform, at rest.
void place_crates(context &ctx) {
  const vec3 spots[] = {{-1.8f, 1.0f, -1.2f}, {-1.8f, 1.95f, -1.2f}, {1.8f, 1.0f, -1.8f}};
  usize i = 0;
  for (auto [e, b] : world(ctx).view<const body3d, const crate>().each()) {
    body3d_set_position(ctx, b.handle, spots[i++ % 3]);
    body3d_set_velocity(ctx, b.handle, {});
  }
}

// Every coin back in its place: the ones picked up were destroyed.
void spawn_coins(context &ctx) {
  entt::registry &reg = world(ctx);
  const auto old = reg.view<coin>();
  const std::vector<entt::entity> picked(old.begin(), old.end());
  reg.destroy(picked.begin(), picked.end());
  for (vec3 spot : coin_spots) {
    const entt::entity e = reg.create();
    reg.emplace<coin>(e);
    reg.emplace<transform3d>(e, transform3d{.position = spot});
    reg.emplace<shape3d_render>(e, shape3d_render{
                                       .shape = {.kind = shape3d_torus, .radius = 0.28f, .thickness = 0.08f},
                                       .color = coin_color,
                                       .material = {.specular = 0.9f, .shininess = 80.0f,
                                                    .emission = {1.0f, 0.7f, 0.2f, 0.35f}}});
    reg.emplace<body3d>(e, body3d_create(ctx, {.shape = shape3d_sphere,
                                               .position = spot,
                                               .radius = 0.4f,
                                               .user = (u64)entt::to_integral(e),
                                               .sensor = true}));
  }
}

void start_run(context &ctx) {
  character3d_set_position(ctx, g.player, start_pos);
  g.move_vel = {};
  g.vertical = 0.0f;
  g.checkpoint = -1;
  g.checkpoint_pos = start_pos;
  g.won = false;
  g.win_t = 0.0f;
  g.jumps = 0;
  g.coins = 0;
  place_crates(ctx);
  spawn_coins(ctx);
  particles3d_clear(ctx);
}

void startup(context &ctx) {
  g = game_state{};
  entt::registry &reg = world(ctx);
  i32 index = 0;
  for (const platform &p : platform_layout) {
    platform made = p;
    // The body's user number is the platform's index + 1, to find which one
    // the player stands on.
    made.body = body3d_create(ctx, {.shape = shape3d_box,
                                    .position = p.center,
                                    .size = p.half * 2.0f,
                                    .user = (u64)(index + 1)});
    g.platforms.push_back(made);
    index++;
  }
  g.mover = platform{g.mover_a, {1.3f, 0.4f, 1.3f}, {}};
  g.mover.body = body3d_create(
      ctx, {.shape = shape3d_box, .position = g.mover_a, .size = g.mover.half * 2.0f, .motion = body3d_kinematic});
  g.goal = body3d_create(ctx, {.shape = shape3d_sphere, .position = goal_pos, .radius = goal_radius, .sensor = true});

  // Crates: dynamic bodies whose entities the physics moves.
  for (i32 i = 0; i < 3; i++) {
    const entt::entity e = reg.create();
    reg.emplace<crate>(e);
    reg.emplace<transform3d>(e);
    reg.emplace<shape3d_render>(e, shape3d_render{.shape = {.kind = shape3d_box, .size = crate_size, .rounding = 0.05f},
                                                  .color = crate_color,
                                                  .material = {.specular = 0.1f}});
    reg.emplace<body3d>(e, body3d_create(ctx, {.shape = shape3d_box,
                                               .size = crate_size,
                                               .motion = body3d_dynamic,
                                               .mass = 8.0f,
                                               .friction = 0.6f}));
  }

  // The hill: its model is both what is drawn and the triangles walked on.
  g.hill = model_load(ctx, "assets/hill.glb");
  body3d_create(ctx, {.position = hill_pos, .model = g.hill});
  const entt::entity hill = reg.create();
  reg.emplace<transform3d>(hill, transform3d{.position = hill_pos});
  reg.emplace<model3d>(hill, model3d{.model = g.hill});
  model_material grass = model_material_get(ctx, g.hill, 0);
  grass.surface.specular = 0.05f;
  model_material_set(ctx, g.hill, 0, grass);

  // The seesaw: a plank on a hinge over a post, tipping 18 degrees each way.
  body3d_create(ctx, {.position = {seesaw_pivot.x, 0.25f, 0.0f}, .size = {0.35f, 0.9f, 0.35f}});
  const entt::entity plank = reg.create();
  reg.emplace<transform3d>(plank);
  reg.emplace<shape3d_render>(plank, shape3d_render{.shape = {.kind = shape3d_box, .size = plank_size, .rounding = 0.04f},
                                                    .color = plank_color,
                                                    .material = {.specular = 0.1f}});
  const body3d_handle plank_body = body3d_create(
      ctx, {.position = seesaw_pivot, .size = plank_size, .motion = body3d_dynamic, .mass = 25.0f, .friction = 0.8f});
  reg.emplace<body3d>(plank, plank_body);
  g.seesaw = joint3d_create(ctx, {.kind = joint3d_hinge,
                                  .a = plank_body,
                                  .anchor = seesaw_pivot,
                                  .axis = {0.0f, 0.0f, 1.0f},
                                  .min = -18.0f,
                                  .max = 18.0f});

  // The goal's glow: a light entity, lit in every 3D pass.
  const entt::entity lamp = reg.create();
  reg.emplace<transform3d>(lamp, transform3d{.position = goal_pos + vec3{0.0f, 2.0f, 0.0f}});
  reg.emplace<light3d_source>(lamp, light3d_source{.color = goal_color, .intensity = 1.6f, .radius = 8.0f});

  physics3d_set_gravity(ctx, {0.0f, -gravity, 0.0f});
  g.player = character3d_create(ctx, {.position = start_pos, .radius = player_radius, .height = player_height});
  g.robot = model_load(ctx, "assets/robot.glb");
  g.idle = model_anim_find(ctx, g.robot, "idle");
  g.run = model_anim_find(ctx, g.robot, "run");
  g.jump = model_anim_find(ctx, g.robot, "jump");
  g.hero = reg.create();
  reg.emplace<transform3d>(g.hero, transform3d{.position = start_pos});
  reg.emplace<character3d>(g.hero, g.player);
  reg.emplace<model3d>(g.hero, model3d{.model = g.robot, .pose = {.anim = g.idle, .blend_anim = g.run}});
  start_run(ctx);

  cursor_set_locked(ctx, true);
  gizmos_set_visible(ctx, false);
  light3d_set(ctx, {.direction = {-0.45f, -1.0f, -0.3f},
                    .color = {0.9f, 0.88f, 0.8f, 1.0f},
                    .ambient = {0.4f, 0.44f, 0.5f, 1.0f},
                    .shadows = true,
                    .shadow_range = 20.0f,
                    .fog_color = sky_color,
                    .fog_density = 0.012f});
  post_fx_set(ctx, {.vignette = 0.2f, .bloom = 0.6f, .bloom_threshold = 0.85f});
}

// The robot's clip: jump in the air, idle blended into run by speed on the
// ground; turned to face its movement, dissolving around a respawn.
void animate_player(context &ctx) {
  entt::registry &reg = world(ctx);
  transform3d &t = reg.get<transform3d>(g.hero);
  model3d &m = reg.get<model3d>(g.hero);
  t.rotation = {0.0f, g.facing_yaw, 0.0f};
  if (!g.grounded && !g.respawning) {
    if (m.pose.anim != g.jump)
      m.pose = model_pose{.anim = g.jump, .loop = false};
  } else {
    if (m.pose.anim != g.idle)
      m.pose = model_pose{.anim = g.idle, .blend_anim = g.run};
    const f32 speed = length(vec2{g.move_vel.x, g.move_vel.z});
    m.pose.blend = move_toward(m.pose.blend, clamp(speed / move_speed, 0.0f, 1.0f), delta(ctx) * 6.0f);
  }
  const f32 dissolve = !g.respawning               ? 0.0f
                       : g.respawn_t < respawn_out ? g.respawn_t / respawn_out
                                                   : 1.0f - (g.respawn_t - respawn_out) / respawn_in;
  m.fx = fx3d{.dissolve = clamp(dissolve, 0.0f, 1.0f), .edge_color = {1.0f, 0.85f, 0.4f, 1.0f}, .grain = 0.12f};

  // The coins spin.
  for (auto [e, ct] : reg.view<transform3d, const coin>().each())
    ct.rotation = {90.0f, g.clock * 120.0f, 0.0f};
}

void update(context &ctx) {
  const f32 dt = delta(ctx);
  if (key_pressed(ctx, key_escape)) {
    g.locked = !g.locked;
    cursor_set_locked(ctx, g.locked);
  }
  if (key_pressed(ctx, key_g))
    gizmos_set_visible(ctx, !gizmos_visible(ctx));
  if (g.locked) {
    const vec2 d = mouse_delta(ctx);
    g.cam_yaw -= d.x * cam_sensitivity;
    g.cam_pitch = clamp(g.cam_pitch + d.y * cam_sensitivity, cam_pitch_min, cam_pitch_max);
  }
  if (key_pressed(ctx, key_space))
    g.jump_want = jump_buffer;

  if (g.respawning) {
    g.respawn_t += dt;
    if (g.respawn_t >= respawn_out + respawn_in)
      g.respawning = false;
  }
  if (g.won) {
    g.win_t += dt;
    if (g.win_t > 2.2f)
      start_run(ctx);
  }
  animate_player(ctx);
}

void win(context &ctx) {
  g.won = true;
  g.win_t = 0.0f;
  screen_flash(ctx, {0.6f, 1.0f, 0.75f, 0.5f}, 0.6f);
  camera_shake(ctx, 0.3f);
  particle_emitter burst = fx::sparkle();
  burst.color_start = goal_color;
  particles3d_spawn(ctx, burst, goal_pos, 60, {.scale = fx_scale});
}

// What the player touched in the last physics step: coins and the goal.
void read_contacts(context &ctx) {
  entt::registry &reg = world(ctx);
  for (i32 i = 0; i < physics3d_contact_count(ctx); i++) {
    const contact3d c = physics3d_contact(ctx, i);
    if (!c.began || c.character.id != g.player.id)
      continue;
    if (c.a.id == g.goal.id && !g.won) {
      win(ctx);
      continue;
    }
    const auto e = (entt::entity)(entt::id_type)body3d_user(ctx, c.a);
    if (c.sensor && reg.valid(e) && reg.all_of<coin>(e)) {
      particle_emitter burst = fx::sparkle();
      burst.color_start = coin_color;
      particles3d_spawn(ctx, burst, reg.get<transform3d>(e).position, 24, {.scale = fx_scale});
      reg.destroy(e); // its body3d goes with it
      g.coins++;
    }
  }
}

void fixed_update(context &ctx) {
  const f32 dt = delta(ctx);
  g.clock += dt;
  body3d_move_kinematic(ctx, g.mover.body, mover_at(g.clock));
  g.mover.center = body3d_transform(ctx, g.mover.body).position;
  read_contacts(ctx);

  // Out of the world: dissolve out, back to the checkpoint, dissolve in.
  const vec3 pos = player_pos(ctx);
  if (!g.respawning && pos.y < fall_y) {
    g.respawning = true;
    g.respawn_t = 0.0f;
    camera_shake(ctx, 0.2f);
  }
  if (g.respawning) {
    if (g.respawn_t >= respawn_out) {
      character3d_set_position(ctx, g.player, g.checkpoint_pos);
      g.move_vel = {};
      g.vertical = 0.0f;
    }
    character3d_set_velocity(ctx, g.player, {});
    return;
  }
  if (g.won) {
    character3d_set_velocity(ctx, g.player, {});
    return;
  }

  // The previous step's outcome.
  const bool was_grounded = g.grounded;
  g.grounded = character3d_grounded(ctx, g.player);
  if (g.grounded) {
    g.air_jump_used = false;
    if (!was_grounded)
      particles3d_spawn(ctx, fx::dust(), pos, 12, {.scale = fx_scale});
    const body3d_handle under = character3d_ground_body(ctx, g.player);
    const i32 index = (i32)body3d_user(ctx, under) - 1;
    if (index > g.checkpoint && index < (i32)g.platforms.size()) {
      g.checkpoint = index;
      const platform &p = g.platforms[(usize)index];
      g.checkpoint_pos = p.center + vec3{0.0f, p.half.y + 0.05f, 0.0f};
    }
  }

  // Horizontal: accelerate toward the camera-relative input.
  vec2 input{};
  if (key_held(ctx, key_w))
    input.x += 1.0f;
  if (key_held(ctx, key_s))
    input.x -= 1.0f;
  if (key_held(ctx, key_d))
    input.y += 1.0f;
  if (key_held(ctx, key_a))
    input.y -= 1.0f;
  const vec3 forward = flat(cam_forward());
  const vec3 right = normalize(cross(forward, vec3{0.0f, 1.0f, 0.0f}));
  vec3 wish{};
  if (length_sq(input) > 0.0f)
    wish = normalize(forward * input.x + right * input.y);
  const vec2 target{wish.x * move_speed, wish.z * move_speed};
  const vec2 now = move_toward(vec2{g.move_vel.x, g.move_vel.z}, target,
                               (g.grounded ? accel : air_accel) * dt);
  g.move_vel = {now.x, 0.0f, now.y};
  if (length_sq(wish) > 0.0f) {
    const f32 want = std::atan2(wish.x, wish.z) * (180.0f / pi);
    const f32 diff = std::fmod(want - g.facing_yaw + 540.0f, 360.0f) - 180.0f;
    g.facing_yaw += clamp(diff, -turn_speed * dt, turn_speed * dt);
  }

  // Vertical: gravity in the air, a jump from the ground (or within coyote
  // time), one more in the air.
  g.jump_want = std::max(0.0f, g.jump_want - dt);
  g.coyote = g.grounded ? coyote_time : std::max(0.0f, g.coyote - dt);
  if (g.grounded)
    g.vertical = 0.0f;
  else
    g.vertical -= gravity * dt;
  if (g.jump_want > 0.0f && (g.coyote > 0.0f || !g.air_jump_used)) {
    const bool from_ground = g.coyote > 0.0f;
    g.vertical = jump_speed;
    g.coyote = 0.0f;
    g.jump_want = 0.0f;
    if (!from_ground)
      g.air_jump_used = true;
    g.jumps++;
    g.grounded = false;
    particles3d_spawn(ctx, fx::dust(), pos, from_ground ? 10 : 16, {.scale = fx_scale});
  }

  // Standing on the moving platform adds its velocity.
  const vec3 ground = g.grounded ? character3d_ground_velocity(ctx, g.player) : vec3{};
  character3d_set_velocity(ctx, g.player, g.move_vel + ground + vec3{0.0f, g.vertical, 0.0f});
}

// Behind and above the player, orbiting with the mouse, pulled in when a
// body is between the camera and the player.
camera3d third_person_camera(const context &ctx) {
  const vec3 look_at = player_pos(ctx) + vec3{0.0f, player_height + 0.3f, 0.0f};
  const vec3 back = -cam_forward();
  const ray3d_hit hit = physics3d_raycast(ctx, {.origin = look_at, .direction = back}, cam_distance);
  const f32 dist = hit.hit ? std::max(hit.distance - 0.3f, 1.0f) : cam_distance;
  return camera3d{.position = look_at + back * dist, .target = look_at, .fovy = 60.0f, .far_plane = 300.0f};
}

void draw_platform(const context &ctx, const platform &p, rgba color) {
  draw_cube3d(ctx, p.center, p.half * 2.0f, color);
  draw_cube3d(ctx, p.center + vec3{0.0f, p.half.y, 0.0f}, {p.half.x * 2.0f, 0.06f, p.half.z * 2.0f},
              platform_top_color);
}

// Entities (the robot, crates, coins, hill, seesaw plank, goal light) are
// drawn by the engine in end_3d; this draws what is not an entity.
void render(context &ctx) {
  begin_3d(ctx, third_person_camera(ctx));
  light3d_add(ctx, {.position = {0.0f, 12.0f, -10.0f}, .color = {1.0f, 0.9f, 0.7f, 1.0f}, .intensity = 1.2f,
                    .radius = 30.0f});

  for (const platform &p : g.platforms)
    draw_platform(ctx, p, platform_color);
  draw_platform(ctx, g.mover, moving_color);
  // The seesaw's post.
  draw_cube3d(ctx, {seesaw_pivot.x, 0.25f, 0.0f}, {0.35f, 0.9f, 0.35f}, platform_color);

  // The goal: a spinning ring, glowing.
  material3d_set(ctx, {.emission = goal_color, .cast_shadows = false});
  draw_shape3d(ctx, {.kind = shape3d_torus, .position = goal_pos, .rotation = {0.0f, g.clock * 90.0f, 0.0f},
                     .radius = goal_radius, .thickness = 0.22f},
               goal_color);
  material3d_set(ctx, {});

  if (gizmos_visible(ctx)) {
    for (usize i = 0; i < g.platforms.size(); i++)
      gizmo_box3d(ctx, g.platforms[i].center, g.platforms[i].half * 2.0f,
                  (i32)i == g.checkpoint ? colors::yellow : colors::green);
    gizmo_point3d(ctx, g.checkpoint_pos, colors::yellow);
    const vec3 base = player_pos(ctx);
    const vec3 v = character3d_velocity(ctx, g.player);
    gizmo_arrow3d(ctx, base, base + v * 0.25f, colors::red);
    char buf[80];
    std::snprintf(buf, sizeof buf, "v=(%.1f, %.1f, %.1f)%s  bập bênh %.0f°", v.x, v.y, v.z, g.grounded ? " đất" : "",
                  joint3d_position(ctx, g.seesaw));
    gizmo_text3d(ctx, base + vec3{0.0f, player_height + 0.4f, 0.0f}, buf);
  }
  end_3d(ctx);
}

void render_ui(context &ctx) {
  const vec2 screen = screen_size(ctx);
  char line[64];
  std::snprintf(line, sizeof line, "Lần nhảy: %d   Xu: %d/%d", g.jumps, g.coins, (i32)std::size(coin_spots));
  draw_text(ctx, line, {16.0f, 14.0f}, 22.0f, colors::white);
  draw_text(ctx, "WASD: chạy   Space: nhảy (2 lần)   Chuột: xoay camera   G: gizmo   Esc: nhả chuột",
            {16.0f, screen.y - 30.0f}, 16.0f, {0.85f, 0.87f, 0.92f, 1.0f});
  if (g.won) {
    const char *msg = "Tới đích!";
    const f32 size = 48.0f;
    const vec2 m = text_measure(ctx, msg, size);
    draw_text(ctx, msg, {(screen.x - m.x) / 2.0f, 70.0f}, size, goal_color);
  }
  if (!g.locked)
    draw_text(ctx, "Esc: khóa chuột để chơi", {16.0f, 44.0f}, 18.0f, colors::white);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "p3d_startup");
  ecs_register(ctx, phase_update, update, "p3d_update");
  ecs_register(ctx, phase_fixed_update, fixed_update, "p3d_move");
  ecs_register(ctx, phase_render, render, "p3d_render");
  ecs_register(ctx, phase_post_render, render_ui, "p3d_ui");
}
} // namespace

mod_desc platformer3d_module() { return mod_desc{.name = "platformer3d", .setup = setup}; }
} // namespace platformer3d
