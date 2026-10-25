// The play scene: the forest, the player, slimes that chase through walls of
// trees by A*, a sword, a chest and an old turtle to talk to.
#include "game.h"
#include <cmath>

namespace td {
namespace {
constexpr vec2 tile{16.0f, 16.0f};
context *g_ctx = nullptr;

rect cell(i32 index) { return rect{{(f32)(index % 8) * 16.0f, (f32)(index / 8) * 16.0f}, tile}; }

const level_object *object_of(context &ctx, entt::entity e) { return world(ctx).try_get<level_object>(e); }

void add_sprite(context &ctx, entt::entity e, i32 frame) {
  world(ctx).emplace<sprite>(e, sprite{.texture = g.sprites, .source = cell(frame), .origin = {0.5f, 1.0f},
                                       .layer = draw_things});
}

// --- prefabs ---

void build_player(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 0);
  reg.emplace<sprite_anim>(e, sprite_anim{.frame_size = tile, .first = 0, .count = 1, .fps = 8.0f});
  reg.emplace<collider>(e, collider{.size = {8.0f, 6.0f},
                                    .offset = {0.0f, -3.0f},
                                    .layer = layer_player,
                                    .mask = layer_world | layer_enemy | layer_thing});
  topdown_body body{};
  body.speed = 78.0f;
  body.dash_speed = 230.0f;
  reg.emplace<topdown_body>(e, body);
  reg.emplace<topdown_input_map>(e, topdown_input_map{g.move_x, g.move_y, g.dash});
  reg.emplace<player_tag>(e);
  g.player = e;
}

void build_slime(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 8);
  reg.emplace<sprite_anim>(e, sprite_anim{.frame_size = tile, .first = 8, .count = 2, .fps = 3.0f});
  reg.emplace<collider>(e, collider{.size = {10.0f, 7.0f},
                                    .offset = {0.0f, -3.5f},
                                    .layer = layer_enemy,
                                    .mask = layer_world | layer_player | layer_sword,
                                    .trigger = true});
  slime s{};
  s.wander = (f32)((i32)reg.get<transform>(e).pos.x % 5) * 0.3f;
  reg.emplace<slime>(e, std::move(s));
  g.slimes_left++;
}

void build_chest(context &ctx, entt::entity e) {
  add_sprite(ctx, e, 16);
  world(ctx).emplace<collider>(e, collider{.size = {12.0f, 8.0f}, .offset = {0.0f, -4.0f}, .layer = layer_thing,
                                           .mask = layer_player});
  world(ctx).emplace<chest_tag>(e);
}

void build_sign(context &ctx, entt::entity e) {
  add_sprite(ctx, e, 22);
  const level_object *obj = object_of(ctx, e);
  world(ctx).emplace<collider>(e, collider{.size = {12.0f, 8.0f}, .offset = {0.0f, -4.0f}, .layer = layer_thing,
                                           .mask = layer_player});
  world(ctx).emplace<sign_tag>(e, sign_tag{obj != nullptr ? obj->props["text"].string_or("") : ""});
}

void build_npc(context &ctx, entt::entity e) {
  add_sprite(ctx, e, 20);
  world(ctx).emplace<sprite_anim>(e, sprite_anim{.frame_size = tile, .first = 20, .count = 2, .fps = 0.7f});
  world(ctx).emplace<collider>(e, collider{.size = {12.0f, 8.0f}, .offset = {0.0f, -4.0f}, .layer = layer_thing,
                                           .mask = layer_player});
  const level_object *obj = object_of(ctx, e);
  world(ctx).emplace<npc_tag>(e, npc_tag{obj != nullptr ? obj->props["dialog"].string_or("") : ""});
}

// A tree: a big sprite sorted by its trunk, with a small collider at the base.
void build_tree(context &ctx, entt::entity e) {
  world(ctx).emplace<sprite>(e, sprite{.texture = g.sprites,
                                       .source = rect{{0.0f, 96.0f}, {32.0f, 32.0f}},
                                       .origin = {0.5f, 1.0f},
                                       .layer = draw_things});
  world(ctx).emplace<collider>(e, collider{.size = {8.0f, 6.0f}, .offset = {0.0f, -3.0f}, .layer = layer_world});
}

// --- helpers ---

void hurt_player(context &ctx, vec2 from) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(g.player))
    return;
  player_tag &p = reg.get<player_tag>(g.player);
  if (p.hurt_timer > 0.0f || p.hp <= 0)
    return;
  p.hp--;
  p.hurt_timer = 1.0f;
  g.hits_taken++;
  const vec2 pos = reg.get<transform>(g.player).pos;
  reg.get<topdown_body>(g.player).velocity = normalize(pos - from) * 220.0f;
  sound_play_once(ctx, g.s_hurt);
  camera_shake(ctx, 0.45f);
  hitstop(ctx, 0.06f);
  sprite_flash(ctx, g.player, {1.0f, 0.3f, 0.3f, 1.0f}, 0.2f);
  pad_rumble(ctx, 0, 0.5f, 0.7f, 0.2f);
  if (p.hp <= 0)
    timer_after(ctx, 0.5f, [](context &c) { scene_fade(c, g.over); }, {.real_time = true});
}

void hit_slime(context &ctx, entt::entity e, vec2 from) {
  entt::registry &reg = world(ctx);
  slime &s = reg.get<slime>(e);
  if (s.stun > 0.05f)
    return; // one swing, one hit
  s.hp--;
  s.stun = 0.3f;
  const vec2 pos = reg.get<transform>(e).pos;
  s.knock = normalize(pos - from) * 150.0f;
  sound_play_once_at(ctx, g.s_hit, 0.9f + 0.2f * random(ctx).unit(), 1.0f);
  sprite_flash(ctx, e, {1.0f, 1.0f, 1.0f, 1.0f}, 0.12f);
  particles_spawn(ctx, fx::sparks(), pos + vec2{0.0f, -5.0f}, 8);
  hitstop(ctx, 0.04f);
  camera_shake(ctx, 0.15f);
  if (s.hp <= 0) {
    particles_spawn(ctx, fx::splash(), pos + vec2{0.0f, -4.0f}, 14);
    reg.destroy(e);
    g.slimes_left--;
    ui_toast(ctx, tr(ctx, "toast.slime"), {.kind = ui_toast_success, .seconds = 1.2f});
  }
}

void swing(context &ctx) {
  entt::registry &reg = world(ctx);
  player_tag &p = reg.get<player_tag>(g.player);
  const topdown_body &body = reg.get<topdown_body>(g.player);
  p.attack_timer = 0.16f;
  p.attack_cooldown = 0.32f;
  sound_play_once(ctx, g.s_swing);
  const entt::entity sword = reg.create();
  reg.emplace<transform>(sword);
  reg.emplace<collider>(sword, collider{.size = {16.0f, 16.0f}, .layer = layer_sword, .mask = layer_enemy, .trigger = true});
  reg.emplace<child_of>(sword, child_of{.parent = g.player, .local = transform{.pos = body.facing * 13.0f + vec2{0.0f, -6.0f}}});
  reg.emplace<sword_tag>(sword);
  reg.emplace<scene_owned>(sword, scene_owned{g.play});
  timer_after(ctx, 0.16f, [sword](context &c) {
    if (world(c).valid(sword))
      world(c).destroy(sword);
  }, {.owner = sword});
}

// --- events ---

void on_enter(const collision_enter &e) {
  context &ctx = *g_ctx;
  entt::registry &reg = world(ctx);
  if (scene_current(ctx).id != g.play.id || !reg.valid(e.self) || !reg.valid(e.other))
    return;
  if (reg.all_of<sword_tag>(e.self) && reg.all_of<slime>(e.other))
    hit_slime(ctx, e.other, reg.get<transform>(g.player).pos);
  else if (e.self == g.player && reg.all_of<slime>(e.other))
    hurt_player(ctx, reg.get<transform>(e.other).pos);
}

void on_stay(const collision_stay &e) {
  context &ctx = *g_ctx;
  entt::registry &reg = world(ctx);
  if (scene_current(ctx).id != g.play.id || e.self != g.player || !reg.valid(e.other))
    return;
  if (reg.all_of<slime>(e.other))
    hurt_player(ctx, reg.get<transform>(e.other).pos); // still touching once the blink ends
}

void on_dash(const body_dashed &e) {
  context &ctx = *g_ctx;
  sound_play_once(ctx, g.s_dash);
  particles_spawn(ctx, fx::dust(), world(ctx).get<transform>(e.entity).pos, 6);
}

// --- systems ---

void slimes(context &ctx) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(g.player))
    return;
  const f32 dt = delta(ctx);
  const vec2 target = reg.get<transform>(g.player).pos;
  const bool player_alive = reg.get<player_tag>(g.player).hp > 0;
  for (auto [e, tr, s, spr] : reg.view<transform, slime, sprite>().each()) {
    vec2 step{};
    if (s.stun > 0.0f) {
      s.stun -= dt;
      step = s.knock * dt;
      s.knock = s.knock * std::max(0.0f, 1.0f - 8.0f * dt);
    } else {
      const f32 dist = distance(tr.pos, target);
      const bool sees = player_alive && dist < 120.0f &&
                        collision_line_of_sight(ctx, tr.pos + vec2{0.0f, -4.0f}, target + vec2{0.0f, -4.0f}, layer_world);
      if (sees && !s.chasing) {
        s.chasing = true;
        s.repath = 0.0f;
      } else if (!sees && dist > 160.0f) {
        s.chasing = false;
      }
      if (s.chasing && player_alive) {
        s.repath -= dt;
        if (s.repath <= 0.0f) {
          s.repath = 0.4f;
          // Paths are made between the centres of the colliders, the same
          // points the grid was built from, not between the feet.
          std::vector<vec2> path;
          nav_find_path(g.nav, tr.pos + vec2{0.0f, -3.5f}, target + vec2{0.0f, -3.0f}, path);
          s.agent.set(std::move(path));
        }
        step = nav_steer(s.agent, tr.pos + vec2{0.0f, -3.5f}) * 42.0f * dt;
      } else {
        s.wander -= dt;
        if (s.wander <= 0.0f) {
          s.wander = 1.0f + random(ctx).unit() * 1.5f;
          s.wander_dir = random(ctx).unit() < 0.4f ? vec2{} : from_angle(random(ctx).unit() * 360.0f);
        }
        step = s.wander_dir * 14.0f * dt;
      }
    }
    if (step.x != 0.0f || step.y != 0.0f) {
      const collision_move_result r = collision_move(ctx, e, step);
      if (r.hit_x || r.hit_y)
        s.wander = 0.0f; // bumped: choose another way
      if (std::abs(step.x) > 0.01f)
        spr.flip_x = step.x < 0.0f;
    }
    if (reg.all_of<sprite_anim>(e))
      reg.get<sprite_anim>(e).fps = s.chasing ? 7.0f : 3.0f;
  }
}

void player_update(context &ctx) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(g.player) || g.paused)
    return;
  const f32 dt = delta(ctx);
  g.run_time += dt;
  player_tag &p = reg.get<player_tag>(g.player);
  const topdown_body &body = reg.get<topdown_body>(g.player);
  sprite &spr = reg.get<sprite>(g.player);
  sprite_anim &anim = reg.get<sprite_anim>(g.player);
  p.hurt_timer -= dt;
  p.attack_timer -= dt;
  p.attack_cooldown -= dt;

  // Blink while invulnerable.
  spr.tint.a = p.hurt_timer > 0.0f && std::fmod(p.hurt_timer * 16.0f, 2.0f) < 1.0f ? 0.3f : 1.0f;
  // Animation from facing and motion.
  const bool up = body.facing.y < -0.3f && std::abs(body.facing.y) >= std::abs(body.facing.x) * 0.7f;
  if (body.moving)
    anim_play(anim, up ? 4 : 1, 2, 9.0f);
  else
    anim_play(anim, up ? 3 : 0, 1, 1.0f);
  if (std::abs(body.facing.x) > 0.3f)
    spr.flip_x = body.facing.x < 0.0f;

  if (p.hp > 0 && !dialog_active(ctx) && p.attack_cooldown <= 0.0f && action_pressed(ctx, g.attack))
    swing(ctx);

  // Something to talk to or open?
  g.near_talk = entt::null;
  std::vector<entt::entity> hits;
  collision_overlap_circle(ctx, circle{reg.get<transform>(g.player).pos + vec2{0.0f, -4.0f}, 14.0f}, &hits, layer_thing);
  for (const entt::entity h : hits)
    if (reg.any_of<sign_tag, npc_tag, chest_tag>(h))
      g.near_talk = h;
  if (g.near_talk != entt::null && !dialog_active(ctx) && action_pressed(ctx, g.interact)) {
    sound_play_once(ctx, g.s_select);
    if (const sign_tag *s = reg.try_get<sign_tag>(g.near_talk)) {
      const std::string text = !s->text.empty() && s->text[0] == '@' ? tr(ctx, s->text.c_str() + 1) : s->text;
      dialog_say(ctx, tr(ctx, "sign.speaker"), text.c_str());
    } else if (reg.all_of<npc_tag>(g.near_talk)) {
      dialog_start(ctx, g.elder);
    } else if (chest_tag *c = reg.try_get<chest_tag>(g.near_talk); c != nullptr && !c->open) {
      if (g.slimes_left > 0) {
        ui_toast(ctx, tr(ctx, "toast.locked"), {.kind = ui_toast_warning});
        return;
      }
      c->open = true;
      reg.get<sprite>(g.near_talk).source = cell(17);
      sound_play_once(ctx, g.s_win);
      particles_spawn(ctx, fx::sparkle(), reg.get<transform>(g.near_talk).pos + vec2{0.0f, -8.0f}, 24);
      timer_after(ctx, 1.0f, [](context &c2) { scene_fade(c2, g.win); }, {.real_time = true});
    }
  }
}

// The sword swing, drawn as a pale arc over the hitbox.
void draw_swing(context &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, tr, col] : reg.view<const transform, const collider, const sword_tag>().each()) {
    const rect r = collider_bounds(tr, col);
    draw_circle(ctx, rect_center(r), 8.0f, {1.0f, 1.0f, 1.0f, 0.55f});
    draw_circle_lines(ctx, rect_center(r), 8.0f, 1.0f, {1.0f, 1.0f, 1.0f, 0.9f});
  }
}

void talk_hint(context &ctx) {
  entt::registry &reg = world(ctx);
  if (g.near_talk == entt::null || !reg.valid(g.near_talk) || dialog_active(ctx))
    return;
  const vec2 pos = reg.get<transform>(g.near_talk).pos + vec2{0.0f, -22.0f + std::sin(elapsed(ctx) * 5.0f)};
  draw_circle(ctx, pos, 5.0f, {0.1f, 0.1f, 0.15f, 0.8f});
  const auto sources = action_sources(ctx, g.interact);
  const char *key = sources.empty() ? "E" : input_source_name(sources.front());
  draw_text(ctx, key, pos - text_measure(ctx, key, 8.0f) * 0.5f, 8.0f, colors::white);
}

void setup(context &ctx) {
  g_ctx = &ctx;
  events(ctx).sink<collision_enter>().connect<&on_enter>();
  events(ctx).sink<collision_stay>().connect<&on_stay>();
  events(ctx).sink<body_dashed>().connect<&on_dash>();
  ecs_register(ctx, phase_fixed_update, sys_desc{.fnc = slimes, .scene = g.play, .name = "slimes"});
  ecs_register(ctx, phase_update, sys_desc{.fnc = player_update, .scene = g.play, .name = "player_update"});
  ecs_register(ctx, phase_render, sys_desc{.fnc = draw_swing, .scene = g.play, .name = "draw_swing"});
  ecs_register(ctx, phase_render, sys_desc{.fnc = talk_hint, .scene = g.play, .name = "talk_hint"});
}
} // namespace

mod_desc play_module() { return mod_desc{.name = "td.play", .setup = setup}; }

void register_prefabs(context &ctx) {
  prefab_register(ctx, {.name = "player", .build = build_player});
  prefab_register(ctx, {.name = "slime", .build = build_slime});
  prefab_register(ctx, {.name = "chest", .build = build_chest});
  prefab_register(ctx, {.name = "sign", .build = build_sign});
  prefab_register(ctx, {.name = "npc", .build = build_npc});
  prefab_register(ctx, {.name = "tree", .build = build_tree});
}

void play_enter(context &ctx) {
  g.slimes_left = 0;
  g.hits_taken = 0;
  g.run_time = 0.0f;
  g.paused = false;
  g.settings_open = false;
  g.player = entt::null;
  g.near_talk = entt::null;
  g.level = level_load(ctx, "assets/map.tmx");
  if (g.level.id == 0) {
    ui_toast(ctx, "Cannot load the map", {.kind = ui_toast_error});
    return;
  }
  // The walls, the trees and the pond are what slimes must walk around.
  g.nav = nav_grid_from_world(ctx, level_bounds(ctx, g.level), {16.0f, 16.0f}, layer_world);
  g.camera = camera_spawn(ctx, 2.0f);
  world(ctx).emplace<scene_owned>(g.camera, scene_owned{g.play});
  world(ctx).emplace<camera_follow>(g.camera, camera_follow{.target = g.player,
                                                            .offset = {0.0f, -6.0f},
                                                            .deadzone = {16.0f, 16.0f},
                                                            .smoothing = 0.12f,
                                                            .lookahead = {12.0f, 12.0f},
                                                            .bounds = level_bounds(ctx, g.level),
                                                            .pixel_snap = true});
  music_crossfade(ctx, g.m_forest, 0.8f);
}

void play_exit(context &ctx) {
  time_set_paused(ctx, false);
  dialog_stop(ctx);
  g.player = entt::null;
}
} // namespace td
