#include <njin.h>

namespace {
// Sprite sheet with 4 columns, each frame 32x32: row 0 is idle (4 frames),
// row 1 is running (6 frames, overflowing into row 2).
constexpr njin::vec2 frame_size{32, 32};
entt::entity hero = entt::null;

void spawn(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  const njin::texture_handle sheet = njin::texture_load(ctx, "assets/hero.png");
  njin::texture_set_filter(ctx, sheet, njin::filter_nearest); // pixel art

  hero = reg.create();
  reg.emplace<njin::transform>(hero, njin::transform{.pos = {100, 100}, .scale = 3});
  reg.emplace<njin::sprite>(hero, njin::sprite{.texture = sheet,
                                               .origin = {0.5f, 1.0f}, // the character's feet
                                               .layer = 10});
  reg.emplace<njin::sprite_anim>(
      hero, njin::sprite_anim{.frame_size = frame_size, .first = 0, .count = 4, .fps = 6});

  // A tree on a lower layer: drawn first, so it sits behind the character.
  const entt::entity tree = reg.create();
  reg.emplace<njin::transform>(tree, njin::transform{.pos = {160, 100}, .scale = 3});
  reg.emplace<njin::sprite>(tree, njin::sprite{.texture = sheet,
                                               .source = {{0, 96}, {32, 32}},
                                               .origin = {0.5f, 1.0f},
                                               .layer = 5});
}

void control(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  auto &tr = reg.get<njin::transform>(hero);
  auto &spr = reg.get<njin::sprite>(hero);
  auto &anim = reg.get<njin::sprite_anim>(hero);

  njin::f32 dir = 0;
  if (njin::key_pressed(ctx, njin::key_a) || njin::key_held(ctx, njin::key_a))
    dir -= 1;
  if (njin::key_pressed(ctx, njin::key_d) || njin::key_held(ctx, njin::key_d))
    dir += 1;

  tr.pos.x += dir * 120.0f * njin::delta(ctx);
  if (dir != 0)
    spr.flip_x = dir < 0; // face the direction of movement

  // Safe to call every frame: it only restarts when switching to a different animation.
  if (dir != 0)
    njin::anim_play(anim, 4, 6, 12.0f);
  else
    njin::anim_play(anim, 0, 4, 6.0f);
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
  njin::ecs_register(ctx, njin::phase_update, control);
}
} // namespace

njin::mod_desc sprites_module() { return {.name = "sprites", .setup = setup}; }
