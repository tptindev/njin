#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace defense {
game_state game;
namespace {
texture_handle enemy_sheet{};
}

vec2 point_on_path(f32 progress) {
  if (progress <= 0.0f)
    return path.front();
  for (usize i = 1; i < path.size(); ++i) {
    if (progress <= game.path_lengths[i]) {
      const f32 segment_length = game.path_lengths[i] - game.path_lengths[i - 1];
      const f32 t = (progress - game.path_lengths[i - 1]) / segment_length;
      return path[i - 1] + (path[i] - path[i - 1]) * t;
    }
  }
  return path.back();
}

f32 point_segment_distance(vec2 p, vec2 a, vec2 b) {
  const vec2 ab = b - a;
  const f32 den = length_sq(ab);
  const f32 t = den > 0.0f ? clamp(dot(p - a, ab) / den, 0.0f, 1.0f) : 0.0f;
  return distance(p, a + ab * t);
}

f32 distance_to_road(vec2 p) {
  f32 closest = 100000.0f;
  for (usize i = 1; i < path.size(); ++i)
    closest = std::min(closest, point_segment_distance(p, path[i - 1], path[i]));
  return closest;
}

void toast(context &ctx, const char *message) {
  ui_toast(ctx, message, {.kind = ui_toast_info, .seconds = 2.0f});
}

void recalc_path() {
  game.path_lengths[0] = 0.0f;
  for (usize i = 1; i < path.size(); ++i)
    game.path_lengths[i] = game.path_lengths[i - 1] + distance(path[i - 1], path[i]);
  game.path_total = game.path_lengths.back();
}

entt::entity spawn_enemy(context &ctx, i32 index) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  const f32 wave_strength = static_cast<f32>(game.wave - 1);
  enemy_component foe{};
  foe.max_hp = 34.0f + wave_strength * 15.0f;
  foe.speed = 43.0f + wave_strength * 2.0f;
  foe.reward = 13 + game.wave / 2;
  foe.kind = foe_kind::regular;
  if (index % 5 == 2) {
    foe.max_hp *= 0.72f;
    foe.speed *= 1.38f;
    foe.reward = 11 + game.wave / 2;
    foe.radius = 10.0f;
    foe.kind = foe_kind::scout;
  } else if (index % 6 == 4) {
    foe.max_hp *= 2.0f;
    foe.speed *= 0.72f;
    foe.reward = 23 + game.wave;
    foe.radius = 15.0f;
    foe.kind = foe_kind::brute;
  }
  foe.hp = foe.max_hp;
  const f32 visual_scale = foe.kind == foe_kind::scout ? 1.15f : foe.kind == foe_kind::brute ? 1.75f : 1.42f;
  const rgba tint = foe.kind == foe_kind::scout ? rgb(230, 247, 167) :
                    foe.kind == foe_kind::brute ? rgb(153, 198, 166) : paper;
  reg.emplace<transform>(e, transform{.pos = point_on_path(0.0f), .scale = visual_scale});
  reg.emplace<enemy_component>(e, foe);
  reg.emplace<sprite>(e, sprite{.texture = enemy_sheet,
                                .source = {{0.0f, 16.0f}, {16.0f, 16.0f}},
                                .origin = {0.5f, 0.5f},
                                .tint = tint,
                                .layer = 20});
  reg.emplace<sprite_anim>(e, sprite_anim{.frame_size = {16.0f, 16.0f}, .first = 8, .count = 2, .fps = 6.0f});
  return e;
}

void launch_wave(context &ctx) {
  if (game.wave_active || game.paused || game.screen != game_screen::playing)
    return;
  if (game.wave >= wave_limit) {
    toast(ctx, "Đã vượt hết các đợt tấn công!");
    return;
  }
  ++game.wave;
  game.spawned = 0;
  game.wave_size = 6 + game.wave * 2;
  game.spawn_timer = 0.15f;
  game.wave_active = true;
  char line[80];
  std::snprintf(line, sizeof line, "ĐỢT %02d  |  %d QUÁI", game.wave, game.wave_size);
  toast(ctx, line);
}

void clear_game_entities(context &ctx) {
  entt::registry &reg = world(ctx);
  std::vector<entt::entity> old;
  for (auto [e, foe] : reg.view<enemy_component>().each()) {
    (void)foe;
    old.push_back(e);
  }
  for (auto [e, tower] : reg.view<tower_component>().each()) {
    (void)tower;
    old.push_back(e);
  }
  for (auto [e, shot] : reg.view<projectile_component>().each()) {
    (void)shot;
    old.push_back(e);
  }
  for (auto [e, number] : reg.view<damage_number_component>().each()) {
    (void)number;
    old.push_back(e);
  }
  for (auto [e, emitter] : reg.view<particle_emitter>().each()) {
    (void)emitter;
    old.push_back(e);
  }
  std::sort(old.begin(), old.end());
  old.erase(std::unique(old.begin(), old.end()), old.end());
  reg.destroy(old.begin(), old.end());
}

void reset_game(context &ctx) {
  clear_game_entities(ctx);
  game.gold = 265;
  game.lives = 20;
  game.wave = 0;
  game.spawned = 0;
  game.wave_size = 0;
  game.build_kind = -1;
  game.speed = 1;
  game.spawn_timer = 0.0f;
  game.wave_active = false;
  game.paused = false;
  game.end_popup_open = false;
  game.selected = entt::null;
  game.screen = game_screen::playing;
  ui_toast_clear(ctx);
  toast(ctx, "Đặt tháp trước khi bấm BẮT ĐẦU ĐỢT!");
}

void return_to_title(context &ctx) {
  clear_game_entities(ctx);
  game.build_kind = -1;
  game.speed = 1;
  game.wave_active = false;
  game.paused = false;
  game.end_popup_open = false;
  game.selected = entt::null;
  game.screen = game_screen::intro;
  ui_toast_clear(ctx);
}

vec2 snapped(vec2 point) {
  const f32 x = field_x + 26.0f + std::round((point.x - field_x - 26.0f) / grid_step) * grid_step;
  const f32 y = field_y + 26.0f + std::round((point.y - field_y - 26.0f) / grid_step) * grid_step;
  return {x, y};
}

bool placement_ok(context &ctx, vec2 pos) {
  if (pos.x < field_x + 24.0f || pos.y < field_y + 24.0f || pos.x > field_x + field_w - 24.0f ||
      pos.y > field_y + field_h - 24.0f || distance_to_road(pos) < road_width * 0.5f + 17.0f)
    return false;
  for (auto [e, tr, tw] : world(ctx).view<const transform, const tower_component>().each()) {
    (void)e;
    (void)tw;
    if (distance(pos, tr.pos) < 45.0f)
      return false;
  }
  return true;
}

void place_tower(context &ctx, vec2 pos) {
  if (game.build_kind < 0 || game.build_kind >= static_cast<i32>(specs.size()))
    return;
  if (!placement_ok(ctx, pos)) {
    toast(ctx, "Không thể đặt tháp lên đường hoặc lên tháp khác");
    return;
  }
  const tower_spec &spec = specs[static_cast<usize>(game.build_kind)];
  if (game.gold < spec.cost) {
    toast(ctx, "Không đủ vàng để xây tháp");
    return;
  }
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos});
  reg.emplace<tower_component>(e, tower_component{.kind = static_cast<tower_kind>(game.build_kind), .cooldown = 0.12f});
  game.gold -= spec.cost;
  game.selected = e;
  game.build_kind = -1;
  toast(ctx, "Đã dựng tháp phòng thủ");
}

void select_tower(context &ctx, vec2 pos) {
  entt::entity nearest = entt::null;
  f32 best = 27.0f;
  for (auto [e, tr, tw] : world(ctx).view<const transform, const tower_component>().each()) {
    (void)tw;
    const f32 d = distance(pos, tr.pos);
    if (d < best) {
      best = d;
      nearest = e;
    }
  }
  game.selected = nearest;
}

void upgrade_selected(context &ctx) {
  if (game.selected == entt::null || !world(ctx).valid(game.selected) || !world(ctx).all_of<tower_component>(game.selected))
    return;
  tower_component &tw = world(ctx).get<tower_component>(game.selected);
  const i32 cost = 38 + tw.level * 32;
  if (game.gold < cost)
    return;
  game.gold -= cost;
  ++tw.level;
  toast(ctx, "Tháp đã được nâng cấp");
}

void sell_selected(context &ctx) {
  if (game.selected == entt::null || !world(ctx).valid(game.selected) || !world(ctx).all_of<tower_component>(game.selected))
    return;
  const tower_component &tw = world(ctx).get<tower_component>(game.selected);
  game.gold += specs[static_cast<usize>(tw.kind)].cost * std::min(85, 58 + tw.level * 7) / 100;
  world(ctx).destroy(game.selected);
  game.selected = entt::null;
  toast(ctx, "Đã thu hồi tháp");
}

void input(context &ctx) {
  if (game.screen != game_screen::playing || !mouse_pressed(ctx, mouse_left))
    return;
  const vec2 mouse = mouse_pos(ctx);
  if (!inside(field, mouse))
    return;
  if (game.build_kind >= 0)
    place_tower(ctx, snapped(mouse));
  else
    select_tower(ctx, mouse);
}

void damage_enemy(context &ctx, vec2 at, f32 damage, f32 radius, tower_kind source) {
  entt::registry &reg = world(ctx);
  struct hit_record {
    entt::entity entity;
    bool killed;
  };
  std::vector<hit_record> hits;
  for (auto [e, tr, foe] : reg.view<const transform, enemy_component>().each()) {
    if (foe.hp <= 0.0f)
      continue;
    if ((radius <= 0.0f && distance(tr.pos, at) < foe.radius + 7.0f) ||
        (radius > 0.0f && distance(tr.pos, at) <= radius + foe.radius)) {
      foe.hp = std::max(0.0f, foe.hp - damage);
      const bool killed = foe.hp <= 0.0f;
      if (killed)
        game.gold += foe.reward;
      hits.push_back({e, killed});
    }
  }
  for (const hit_record &hit : hits)
    enemy_hit_effect(ctx, hit.entity, damage, source, hit.killed);
}

void update_game(context &ctx) {
  if (game.paused)
    return;
  const f32 dt = delta(ctx) * static_cast<f32>(game.speed);
  entt::registry &reg = world(ctx);

  if (game.wave_active && game.spawned < game.wave_size) {
    game.spawn_timer -= dt;
    if (game.spawn_timer <= 0.0f) {
      spawn_enemy(ctx, game.spawned);
      ++game.spawned;
      game.spawn_timer = std::max(0.42f, 0.88f - static_cast<f32>(game.wave) * 0.025f);
    }
  }

  std::vector<entt::entity> leaked;
  for (auto [e, tr, foe] : reg.view<transform, enemy_component>().each()) {
    if (foe.hp <= 0.0f)
      continue;
    foe.progress += foe.speed * dt;
    tr.pos = point_on_path(foe.progress);
    if (foe.progress >= game.path_total) {
      --game.lives;
      leaked.push_back(e);
    }
  }
  reg.destroy(leaked.begin(), leaked.end());
  if (game.lives <= 0) {
    game.lives = 0;
    game.screen = game_screen::ended;
    game.wave_active = false;
    game.paused = true;
    game.end_popup_open = true;
    return;
  }

  struct pending_shot {
    vec2 origin;
    entt::entity target;
    vec2 target_pos;
    f32 speed;
    f32 damage;
    f32 splash;
    rgba color;
    tower_kind kind;
  };
  std::vector<pending_shot> new_shots;
  for (auto [e, tr, tower] : reg.view<transform, tower_component>().each()) {
    tower.cooldown = std::max(0.0f, tower.cooldown - dt);
    entt::entity target = entt::null;
    f32 furthest = -1.0f;
    const tower_spec &spec = specs[static_cast<usize>(tower.kind)];
    const f32 range = spec.range + static_cast<f32>(tower.level - 1) * 8.0f;
    for (auto [foe_entity, foe_tr, foe] : reg.view<const transform, const enemy_component>().each()) {
      if (foe.hp > 0.0f && distance(tr.pos, foe_tr.pos) <= range && foe.progress > furthest) {
        target = foe_entity;
        furthest = foe.progress;
      }
    }
    if (tower.cooldown > 0.0f || target == entt::null)
      continue;
    const vec2 target_pos = reg.get<transform>(target).pos;
    new_shots.push_back({
        tr.pos,
        target,
        target_pos,
        spec.projectile_speed,
        spec.damage * (1.0f + 0.34f * static_cast<f32>(tower.level - 1)),
        spec.splash + static_cast<f32>(tower.level - 1) * 5.0f,
        spec.color,
        tower.kind,
    });
    tower_fired_effect(ctx, e);
    tower.cooldown = std::max(0.27f, spec.reload * (1.0f - static_cast<f32>(tower.level - 1) * 0.065f));
    (void)e;
  }
  for (const pending_shot &shot : new_shots) {
    const entt::entity bolt = reg.create();
    reg.emplace<transform>(bolt, transform{.pos = shot.origin});
    reg.emplace<projectile_component>(bolt, projectile_component{
        .target = shot.target,
        .last_target = shot.target_pos,
        .kind = shot.kind,
        .speed = shot.speed,
        .damage = shot.damage,
        .splash = shot.splash,
        .color = shot.color,
    });
  }

  struct impact {
    vec2 at;
    f32 damage;
    f32 radius;
    tower_kind kind;
  };
  std::vector<impact> impacts;
  std::vector<entt::entity> spent;
  for (auto [e, tr, bolt] : reg.view<transform, projectile_component>().each()) {
    if (reg.valid(bolt.target) && reg.all_of<enemy_component, transform>(bolt.target))
      bolt.last_target = reg.get<transform>(bolt.target).pos;
    const vec2 offset = bolt.last_target - tr.pos;
    const f32 remaining = length(offset);
    const f32 step = bolt.speed * dt;
    if (remaining <= step + 4.0f) {
      impacts.push_back({bolt.last_target, bolt.damage, bolt.splash, bolt.kind});
      spent.push_back(e);
    } else {
      tr.pos += normalize(offset) * step;
    }
  }
  reg.destroy(spent.begin(), spent.end());
  for (const impact &hit : impacts)
    damage_enemy(ctx, hit.at, hit.damage, hit.radius, hit.kind);

  if (game.wave_active && game.spawned >= game.wave_size && reg.view<enemy_component>().empty()) {
    game.wave_active = false;
    game.gold += 28 + game.wave * 3;
    if (game.wave >= wave_limit) {
      game.screen = game_screen::ended;
      game.paused = true;
      game.end_popup_open = true;
    } else {
      toast(ctx, "Bảo vệ thành công! Nhấn nút trong kho để tiếp tục");
    }
  }
}

void startup(context &ctx) {
  recalc_path();
  enemy_sheet = texture_load(ctx, "assets/slime_sheet.png");
  texture_set_filter(ctx, enemy_sheet, filter_nearest);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_pre_update, input, "input");
  ecs_register(ctx, phase_update, update_game, "tower_defense");
  ecs_register(ctx, phase_pre_render, draw_background, "battlefield_background");
  ecs_register(ctx, phase_render, draw_field, "battlefield_actors");
  ecs_register(ctx, phase_post_render, draw_damage_numbers, "damage_numbers");
  ecs_register(ctx, phase_post_render, draw_sidebar, "armory");
  ecs_register(ctx, phase_post_render, draw_overlay, "title_overlay");
}

mod_desc module() {
  return {.name = "tower_defense", .setup = setup};
}
} // namespace defense
