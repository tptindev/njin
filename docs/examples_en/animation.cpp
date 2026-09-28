#include <njin.h>

namespace {
// The character uses a sheet exported from Aseprite with 4 tags: idle, run, jump, attack.
njin::anim_graph_handle hero_anim{};
entt::entity hero = entt::null;
bool grounded = true;
njin::f32 vel_y = 0.0f;

void spawn(njin::njin_ctx &ctx) {
  const njin::anim_sheet_handle sheet = njin::anim_sheet_load(ctx, "assets/hero.json");
  hero_anim = njin::anim_graph_create(ctx, {
      .sheet = sheet,
      .states = {{.name = "idle", .clip = "idle"},
                 {.name = "run", .clip = "run"},
                 {.name = "jump", .clip = "jump", .repeat = 1}, // plays once, then stays on the last frame
                 {.name = "attack", .clip = "attack", .repeat = 1}},
      // Checked from top to bottom; the first one that matches wins.
      .transitions = {
          {.to = "attack", .when = {{"attack", njin::anim_trigger}}},  // from any state
          {.from = "attack", .to = "idle", .after_finish = true},     // back to idle once the attack ends
          {.from = "idle", .to = "jump", .when = {{"grounded", njin::anim_false}}},
          {.from = "run", .to = "jump", .when = {{"grounded", njin::anim_false}}},
          {.from = "jump", .to = "idle", .when = {{"grounded", njin::anim_true}}},
          {.from = "idle", .to = "run", .when = {{"speed", njin::anim_gt, 10}}},
          {.from = "run", .to = "idle", .when = {{"speed", njin::anim_le, 10}}},
      }});

  entt::registry &reg = njin::world(ctx);
  hero = reg.create();
  reg.emplace<njin::transform>(hero, njin::transform{.pos = {200, 300}, .scale = 3});
  reg.emplace<njin::sprite>(hero, njin::sprite{.origin = {0.5f, 1.0f}});
  reg.emplace<njin::animator>(hero, njin::animator{.graph = hero_anim});
}

void control(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  auto &tr = reg.get<njin::transform>(hero);
  auto &anim = reg.get<njin::animator>(hero);
  const njin::f32 dt = njin::delta(ctx);

  njin::f32 dir = 0;
  if (njin::key_held(ctx, njin::key_a))
    dir -= 1;
  if (njin::key_held(ctx, njin::key_d))
    dir += 1;
  if (grounded && njin::key_pressed(ctx, njin::key_space)) {
    vel_y = -420.0f;
    grounded = false;
  }
  vel_y += 1200.0f * dt;
  tr.pos += njin::vec2{dir * 160.0f, vel_y} * dt;
  if (tr.pos.y >= 300.0f) { // ground
    tr.pos.y = 300.0f;
    vel_y = 0.0f;
    grounded = true;
  }
  if (dir != 0)
    reg.get<njin::sprite>(hero).flip_x = dir < 0;

  // The game only reports the situation; the graph picks the animation itself.
  njin::animator_set(ctx, anim, "speed", dir != 0 ? 160.0f : 0.0f);
  njin::animator_set_bool(ctx, anim, "grounded", grounded);
  if (njin::key_pressed(ctx, njin::key_j))
    njin::animator_trigger(ctx, anim, "attack");

  // Lock movement while attacking.
  if (njin::animator_in(ctx, anim, "attack"))
    tr.pos.x -= dir * 160.0f * dt;
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
  njin::ecs_register(ctx, njin::phase_update, control);
}
} // namespace

njin::mod_desc animation_module() { return {.name = "animation", .setup = setup}; }
