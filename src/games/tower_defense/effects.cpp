#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace defense {
namespace {
void spawn_damage_number(njin_ctx &ctx, vec2 at, i32 amount, rgba color) {
  entt::registry &reg = world(ctx);
  const entt::entity number = reg.create();
  const vec2 start = at + vec2{0.0f, -11.0f};
  reg.emplace<transform>(number, transform{.pos = start});
  reg.emplace<damage_number_component>(number, damage_number_component{.amount = amount, .color = color});

  constexpr f32 lifetime = 0.62f;
  tween_move(ctx, number, start + vec2{0.0f, -30.0f}, lifetime, ease::out_cubic);
  tween_value(ctx, 1.0f, 0.0f, lifetime,
              [number](njin_ctx &c, f32 alpha) {
                entt::registry &registry = world(c);
                if (registry.valid(number) && registry.all_of<damage_number_component>(number))
                  registry.get<damage_number_component>(number).alpha = alpha;
              },
              ease::out_quad,
              {.done = [number](njin_ctx &c) {
                 entt::registry &registry = world(c);
                 if (registry.valid(number))
                   registry.destroy(number);
               }});
}

void spawn_impact_particles(njin_ctx &ctx, vec2 at, tower_kind source) {
  particle_emitter emitter{};
  i32 count = 0;
  if (source == tower_kind::mage) {
    emitter = fx::sparks();
    emitter.color_start = rgb(224, 195, 255);
    emitter.color_end = rgb(127, 90, 255, 0);
    emitter.size_start = 5.0f;
    emitter.size_end = 0.5f;
    emitter.blend = blend_additive;
    count = 12;
  } else if (source == tower_kind::cannon) {
    emitter = fx::explosion();
    emitter.color_start = rgb(255, 218, 139);
    emitter.color_end = rgb(239, 91, 54, 0);
    emitter.size_start = 5.0f;
    emitter.size_end = 0.0f;
    emitter.blend = blend_additive;
    count = 18;
  } else {
    return;
  }
  emitter.layer = 40;
  particles_spawn(ctx, emitter, at, count);
}
} // namespace

void tower_fired_effect(njin_ctx &ctx, entt::entity tower) {
  tween_scale(ctx, tower, 1.2f, 0.075f, ease::out_back, {.repeat = 1, .yoyo = true});
}

void enemy_hit_effect(njin_ctx &ctx, entt::entity enemy, f32 damage, tower_kind source, bool killed) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(enemy) || !reg.all_of<transform, enemy_component, sprite>(enemy))
    return;

  const vec2 at = reg.get<transform>(enemy).pos;
  sprite_flash(ctx, enemy, rgb(255, 255, 255), 0.11f);
  spawn_damage_number(ctx, at, std::max(1, static_cast<i32>(std::round(damage))), specs[static_cast<usize>(source)].color);
  spawn_impact_particles(ctx, at, source);

  if (killed) {
    sprite_dissolve(ctx, enemy, 0.62f);
    dissolve_fx &dissolve = reg.get<dissolve_fx>(enemy);
    dissolve.edge_color = rgb(255, 183, 104);
    dissolve.edge_width = 2.0f;
    dissolve.grain = 2.0f;
    dissolve.seed = static_cast<u32>(game.wave * 313 + static_cast<i32>(reg.get<enemy_component>(enemy).progress));
    dissolve.destroy_when_done = true;
  }
}

void draw_damage_numbers(njin_ctx &ctx) {
  for (auto [e, tr, number] : world(ctx).view<const transform, const damage_number_component>().each()) {
    (void)e;
    char label[16];
    std::snprintf(label, sizeof label, "%d", number.amount);
    constexpr f32 size = 18.0f;
    const vec2 measured = text_measure(ctx, label, size);
    const vec2 pos{tr.pos.x - measured.x * 0.5f, tr.pos.y - measured.y * 0.5f};
    draw_text(ctx, label, pos + vec2{0.0f, 2.0f}, size, rgba{0.035f, 0.07f, 0.08f, number.alpha * 0.8f});
    rgba color = number.color;
    color.a *= number.alpha;
    draw_text(ctx, label, pos, size, color);
  }
}
} // namespace defense
