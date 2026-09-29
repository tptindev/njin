#include <njin.h>

namespace {
// Sprite sheet 4 cột, mỗi frame 32x32: hàng 0 là đứng yên (4 frame),
// hàng 1 là chạy (6 frame, tràn sang hàng 2).
constexpr njin::vec2 frame_size{32, 32};
entt::entity hero = entt::null;

void spawn(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  const njin::texture_handle sheet = njin::texture_load(ctx, "assets/hero.png");
  njin::texture_set_filter(ctx, sheet, njin::filter_nearest); // pixel art

  hero = reg.create();
  reg.emplace<njin::transform>(hero, njin::transform{.pos = {100, 100}, .scale = 3});
  reg.emplace<njin::sprite>(hero, njin::sprite{.texture = sheet,
                                               .origin = {0.5f, 1.0f}, // chân nhân vật
                                               .layer = 10});
  reg.emplace<njin::sprite_anim>(
      hero, njin::sprite_anim{.frame_size = frame_size, .first = 0, .count = 4, .fps = 6});

  // Một cái cây ở lớp thấp hơn: vẽ trước, nằm sau nhân vật.
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
    spr.flip_x = dir < 0; // quay mặt theo hướng đi

  // Gọi mỗi frame được: chỉ bắt đầu lại khi đổi sang animation khác.
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
