// The play scene: the level, the player, enemies, pickups, and the rules.
#include "game.h"
#include <cmath>

namespace plat {
namespace {
constexpr vec2 tile{16.0f, 16.0f};

rect cell(i32 index) {
  return rect{{(f32)(index % 8) * 16.0f, (f32)(index / 8) * 16.0f}, tile};
}

const level_object *object_of(context &ctx, entt::entity e) {
  return world(ctx).try_get<level_object>(e);
}

void add_sprite(context &ctx, entt::entity e, i32 frame, i32 layer = draw_items) {
  world(ctx).emplace<sprite>(e, sprite{.texture = g.sprites,
                                       .source = cell(frame),
                                       .origin = {0.5f, 1.0f},
                                       .layer = layer});
}

void add_anim(context &ctx, entt::entity e, i32 first, i32 count, f32 fps) {
  world(ctx).emplace<sprite_anim>(
      e, sprite_anim{.frame_size = tile, .first = first, .count = count, .fps = fps});
}

// --- prefabs: the level loader builds every object through the prefab
// named after its type (Tiled "class"), with its level_object attached ---

void build_player(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 0, draw_player);
  add_anim(ctx, e, 0, 2, 2.0f);
  reg.emplace<collider>(e, collider{.size = {10.0f, 13.0f},
                                    .offset = {0.0f, -6.5f},
                                    .layer = layer_player,
                                    .mask = layer_world | layer_enemy | layer_pickup | layer_hazard});
  platformer_body body{};
  body.jump_speed = 350.0f;
  body.wall_slide_speed = 55.0f;
  body.wall_jump = {150.0f, 300.0f};
  reg.emplace<platformer_body>(e, body);
  reg.emplace<platformer_input_map>(e, platformer_input_map{g.move, g.jump, g.down});
  reg.emplace<player_tag>(e);
  g.player = e;
  g.respawn = reg.get<transform>(e).pos;
}

void build_coin(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 16);
  add_anim(ctx, e, 16, 4, 8.0f);
  reg.emplace<collider>(e, collider{.shape = collider_circle,
                                    .radius = 5.0f,
                                    .offset = {0.0f, -8.0f},
                                    .layer = layer_pickup,
                                    .mask = layer_player,
                                    .trigger = true});
  reg.emplace<coin_tag>(e);
  // Bob up and down, forever.
  const vec2 pos = reg.get<transform>(e).pos;
  tween_move(ctx, e, pos + vec2{0.0f, -2.0f}, 0.6f, ease::in_out_quad,
             {.delay = (f32)((i32)pos.x % 7) * 0.08f, .repeat = -1, .yoyo = true});
  g.coins_total++;
}

void build_walker(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 24);
  add_anim(ctx, e, 24, 2, 5.0f);
  // A trigger: it hurts the player but never blocks it.
  reg.emplace<collider>(e, collider{.size = {12.0f, 10.0f},
                                    .offset = {0.0f, -5.0f},
                                    .layer = layer_enemy,
                                    .mask = layer_world | layer_player,
                                    .trigger = true});
  reg.emplace<walker>(e);
}

void build_checkpoint(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 32);
  reg.emplace<collider>(e, collider{.size = {10.0f, 16.0f},
                                    .offset = {0.0f, -8.0f},
                                    .layer = layer_pickup,
                                    .mask = layer_player,
                                    .trigger = true});
  reg.emplace<checkpoint>(e);
}

void build_exit(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 35);
  add_anim(ctx, e, 35, 2, 4.0f);
  reg.emplace<collider>(e, collider{.size = {10.0f, 14.0f},
                                    .offset = {0.0f, -7.0f},
                                    .layer = layer_pickup,
                                    .mask = layer_player,
                                    .trigger = true});
  reg.emplace<exit_tag>(e);
}

void build_spikes(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 40);
  reg.emplace<collider>(e, collider{.size = {14.0f, 7.0f},
                                    .offset = {0.0f, -3.5f},
                                    .layer = layer_hazard,
                                    .mask = layer_player,
                                    .trigger = true});
  reg.emplace<hazard_tag>(e);
}

// Water: an invisible rectangle drawn in Tiled.
void build_hazard(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  const level_object *obj = object_of(ctx, e);
  reg.emplace<collider>(e, collider{.size = obj != nullptr ? obj->size - vec2{0.0f, 4.0f} : tile,
                                    .offset = {0.0f, 4.0f},
                                    .layer = layer_hazard,
                                    .mask = layer_player,
                                    .trigger = true});
  reg.emplace<hazard_tag>(e);
}

void build_sign(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 41);
  reg.emplace<collider>(e, collider{.size = {24.0f, 16.0f},
                                    .offset = {0.0f, -8.0f},
                                    .layer = layer_pickup,
                                    .mask = layer_player,
                                    .trigger = true});
  const level_object *obj = object_of(ctx, e);
  reg.emplace<sign>(e, sign{obj != nullptr ? obj->props["text"].string_or("") : ""});
}

void build_npc(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  add_sprite(ctx, e, 42);
  add_anim(ctx, e, 42, 2, 0.8f);
  reg.emplace<collider>(e, collider{.size = {24.0f, 16.0f},
                                    .offset = {0.0f, -8.0f},
                                    .layer = layer_pickup,
                                    .mask = layer_player,
                                    .trigger = true});
  const level_object *obj = object_of(ctx, e);
  reg.emplace<npc>(e, npc{obj != nullptr ? obj->props["dialog"].string_or("") : ""});
}

// A moving platform: a polyline in Tiled, whose points are its stops.
void build_platform(context &ctx, entt::entity e) {
  entt::registry &reg = world(ctx);
  const level_object *obj = object_of(ctx, e);
  transform &tr = reg.get<transform>(e);
  reg.emplace<sprite>(e, sprite{.texture = g.sprites,
                                .source = rect{{0.0f, 96.0f}, {48.0f, 8.0f}},
                                .origin = {0.5f, 0.5f},
                                .layer = draw_items});
  reg.emplace<collider>(e, collider{.size = {48.0f, 8.0f}, .layer = layer_world, .one_way = true});
  path_mover path{};
  if (obj != nullptr) {
    for (const vec2 p : obj->points)
      path.points.push_back(tr.pos + p);
    path.speed = obj->props["speed"].f32_or(30.0f);
    path.wait = obj->props["wait"].f32_or(0.0f);
  }
  if (!path.points.empty())
    tr.pos = path.points.front();
  reg.emplace<path_mover>(e, std::move(path));
}

// --- rules ---

void respawn(context &ctx) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(g.player))
    return;
  reg.get<transform>(g.player).pos = g.respawn;
  platformer_body &body = reg.get<platformer_body>(g.player);
  body.velocity = {};
  body.input = {};
  reg.get<collider>(g.player).enabled = true;
  if (camera_follow *f = reg.try_get<camera_follow>(g.camera))
    f->started = false; // snap to the player instead of sliding across the level
  particles_spawn(ctx, fx::sparkle(), g.respawn + vec2{0.0f, -8.0f}, 16);
  g.dying = false;
}

void kill_player(context &ctx) {
  entt::registry &reg = world(ctx);
  if (g.dying || g.finished || !reg.valid(g.player))
    return;
  g.dying = true;
  g.deaths++;
  sound_play_once(ctx, g.s_hurt);
  camera_shake(ctx, 0.5f);
  hitstop(ctx, 0.08f);
  screen_flash(ctx, {0.9f, 0.2f, 0.2f, 0.35f}, 0.2f);
  pad_rumble(ctx, 0, 0.6f, 0.8f, 0.25f);
  // Pop up and fall off the screen, like the classics: no collisions.
  platformer_body &body = reg.get<platformer_body>(g.player);
  body.input = {};
  body.velocity = {0.0f, -260.0f};
  reg.get<collider>(g.player).enabled = false;
  timer_after(ctx, 0.9f, respawn, {.owner = g.player});
}

void finish_level(context &ctx) {
  if (g.finished || g.dying)
    return;
  g.finished = true;
  sound_play_once(ctx, g.s_win);
  g.run_coins += g.coins;
  g.run_total += g.coins_total;
  const char *next = level_properties(ctx, g.level)["next"].string_or("");
  if (*next == '\0') {
    scene_fade(ctx, g.win);
  } else {
    g.level_file = std::string("assets/") + next;
    scene_fade(ctx, scene_find(ctx, "card"));
  }
}

void collect(context &ctx, entt::entity coin) {
  entt::registry &reg = world(ctx);
  reg.remove<collider>(coin);
  reg.remove<coin_tag>(coin);
  g.coins++;
  sound_play_once_at(ctx, g.s_coin, 1.0f + 0.05f * (f32)(g.coins % 5), 1.0f);
  const vec2 pos = reg.get<transform>(coin).pos;
  particles_spawn(ctx, fx::sparkle(), pos + vec2{0.0f, -8.0f}, 8);
  tween_move(ctx, coin, pos + vec2{0.0f, -18.0f}, 0.35f, ease::out_cubic);
  tween_tint(ctx, coin, {1.0f, 1.0f, 1.0f, 0.0f}, 0.35f, ease::linear,
             {.done = [coin](context &c) {
               if (world(c).valid(coin))
                 world(c).destroy(coin);
             }});
  if (g.coins == g.coins_total)
    ui_toast(ctx, tr(ctx, "toast.all_coins"), {.kind = ui_toast_success});
}

void stomp(context &ctx, entt::entity enemy) {
  entt::registry &reg = world(ctx);
  walker &w = reg.get<walker>(enemy);
  w.squashed = true;
  reg.remove<collider>(enemy);
  reg.remove<sprite_anim>(enemy);
  reg.get<sprite>(enemy).source = cell(26);
  timer_after(ctx, 0.5f, [enemy](context &c) {
    if (world(c).valid(enemy))
      world(c).destroy(enemy);
  });
  platformer_body &body = reg.get<platformer_body>(g.player);
  body.velocity.y = -230.0f;
  sound_play_once(ctx, g.s_stomp);
  hitstop(ctx, 0.04f);
  camera_shake(ctx, 0.2f);
  particles_spawn(ctx, fx::dust(), reg.get<transform>(enemy).pos, 10);
}

void on_contact(context &ctx, entt::entity other) {
  entt::registry &reg = world(ctx);
  if (reg.all_of<coin_tag>(other)) {
    collect(ctx, other);
  } else if (walker *w = reg.try_get<walker>(other); w != nullptr && !w->squashed) {
    const platformer_body &body = reg.get<platformer_body>(g.player);
    const f32 feet = reg.get<transform>(g.player).pos.y;
    const f32 head = reg.get<transform>(other).pos.y - 10.0f;
    if (body.velocity.y > 0.0f && feet <= head + 6.0f)
      stomp(ctx, other);
    else
      kill_player(ctx);
  } else if (reg.all_of<hazard_tag>(other)) {
    kill_player(ctx);
  } else if (checkpoint *cp = reg.try_get<checkpoint>(other); cp != nullptr && !cp->on) {
    cp->on = true;
    g.respawn = reg.get<transform>(other).pos;
    reg.emplace_or_replace<sprite_anim>(
        other, sprite_anim{.frame_size = tile, .first = 33, .count = 2, .fps = 6.0f});
    sound_play_once(ctx, g.s_check);
    ui_toast(ctx, tr(ctx, "toast.checkpoint"), {.kind = ui_toast_success, .seconds = 1.5f});
  } else if (reg.all_of<exit_tag>(other)) {
    finish_level(ctx);
  }
}

context *g_ctx = nullptr; // for the event handlers below

void on_enter(const collision_enter &e) {
  context &ctx = *g_ctx;
  entt::registry &reg = world(ctx);
  if (scene_current(ctx).id != g.play.id || e.self != g.player || !reg.valid(e.self) ||
      !reg.valid(e.other) || g.dying)
    return;
  on_contact(ctx, e.other);
}

void on_jump(const body_jumped &e) {
  if (e.entity != g.player)
    return;
  context &ctx = *g_ctx;
  sound_play_once(ctx, g.s_jump);
  particles_spawn(ctx, fx::dust(), world(ctx).get<transform>(e.entity).pos, 6);
}

void on_land(const body_landed &e) {
  if (e.entity != g.player || e.speed < 120.0f)
    return;
  context &ctx = *g_ctx;
  sound_play_once_at(ctx, g.s_land, 1.0f, clamp(e.speed / 360.0f, 0.3f, 1.0f));
  particles_spawn(ctx, fx::dust(), world(ctx).get<transform>(e.entity).pos, 8);
  if (e.speed > 330.0f)
    camera_shake(ctx, 0.15f);
}

void on_dialog_end(const dialog_ended &) {
  // Keys pressed while talking must not become a jump afterwards.
  context &ctx = *g_ctx;
  if (world(ctx).valid(g.player))
    world(ctx).get<platformer_body>(g.player).input = {};
}

// --- systems ---

void walkers(context &ctx) {
  entt::registry &reg = world(ctx);
  const f32 dt = delta(ctx);
  for (auto [e, tr, w, spr] : reg.view<transform, walker, sprite>().each()) {
    if (w.squashed)
      continue;
    w.fall = std::min(w.fall + 900.0f * dt, 300.0f);
    const collision_move_result r = collision_move(ctx, e, {w.dir * 28.0f * dt, w.fall * dt});
    if (r.grounded)
      w.fall = 0.0f;
    if (r.hit_x) {
      w.dir = -w.dir;
    } else if (r.grounded) {
      // Turn back at a ledge: nothing under the front foot.
      const vec2 foot = tr.pos + vec2{w.dir * 7.0f, -2.0f};
      if (!collision_raycast(ctx, foot, foot + vec2{0.0f, 10.0f}, layer_world, false, e).hit)
        w.dir = -w.dir;
    }
    spr.flip_x = w.dir > 0.0f;
  }
}

void player_update(context &ctx) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(g.player) || g.paused)
    return;
  g.run_time += delta(ctx);
  const transform &xf = reg.get<transform>(g.player);
  platformer_body &body = reg.get<platformer_body>(g.player);
  sprite &spr = reg.get<sprite>(g.player);
  sprite_anim &anim = reg.get<sprite_anim>(g.player);

  // Fell out of the level.
  const rect bounds = level_bounds(ctx, g.level);
  if (!g.dying && xf.pos.y > bounds.pos.y + bounds.size.y + 32.0f)
    kill_player(ctx);
  if (g.dying) {
    anim.playing = false;
    spr.source = cell(9);
    return;
  }

  // Pick the animation from what the body is doing.
  if (body.grounded) {
    if (std::abs(body.velocity.x) > 10.0f)
      anim_play(anim, 2, 4, 12.0f);
    else
      anim_play(anim, 0, 2, 2.0f);
  } else if (body.on_wall != 0 && body.velocity.y > 0.0f) {
    anim_play(anim, 8, 1, 1.0f);
  } else {
    anim_play(anim, body.velocity.y < 0.0f ? 6 : 7, 1, 1.0f);
  }
  spr.flip_x = body.facing < 0;

  // Something to talk to within reach?
  g.near_talk = entt::null;
  std::vector<entt::entity> hits;
  collision_overlap_rect(ctx, collider_bounds(xf, reg.get<collider>(g.player)), &hits, layer_pickup);
  for (const entt::entity h : hits)
    if (reg.any_of<sign, npc>(h))
      g.near_talk = h;
  if (g.near_talk != entt::null && !dialog_active(ctx) && action_pressed(ctx, g.interact)) {
    if (const sign *s = reg.try_get<sign>(g.near_talk)) {
      const std::string text = !s->text.empty() && s->text[0] == '@' ? tr(ctx, s->text.c_str() + 1) : s->text;
      dialog_say(ctx, tr(ctx, "sign.speaker"), text.c_str());
    } else if (reg.all_of<npc>(g.near_talk)) {
      dialog_start(ctx, g.owl);
    }
    sound_play_once(ctx, g.s_select);
  }
}

// Distant hills behind the level, moving slower than the camera.
void backdrop(context &ctx) {
  const camera_view view = camera_active(ctx);
  draw_backdrop(ctx, view.target);
}

// "E" over whatever the player can talk to.
void talk_hint(context &ctx) {
  entt::registry &reg = world(ctx);
  if (g.near_talk == entt::null || !reg.valid(g.near_talk) || dialog_active(ctx))
    return;
  const vec2 pos = reg.get<transform>(g.near_talk).pos + vec2{0.0f, -24.0f + std::sin(elapsed(ctx) * 5.0f)};
  draw_circle(ctx, pos, 8.0f, {0.1f, 0.1f, 0.15f, 0.8f});
  const char *key = "E";
  const auto sources = action_sources(ctx, g.interact);
  if (!sources.empty())
    key = input_source_name(sources.front());
  const font_handle font = ui_style_get(ctx).font; // the pixel font, at its design size
  draw_text(ctx, key, pos - text_measure(ctx, key, 16.0f, font) * 0.5f, 16.0f, colors::white, font);
}

void setup(context &ctx) {
  g_ctx = &ctx;
  events(ctx).sink<collision_enter>().connect<&on_enter>();
  events(ctx).sink<body_jumped>().connect<&on_jump>();
  events(ctx).sink<body_landed>().connect<&on_land>();
  events(ctx).sink<dialog_ended>().connect<&on_dialog_end>();
  ecs_register(ctx, phase_fixed_update, sys_desc{.fnc = walkers, .scene = g.play, .name = "walkers"});
  ecs_register(ctx, phase_update, sys_desc{.fnc = player_update, .scene = g.play, .name = "player_update"});
  ecs_register(ctx, phase_pre_render, sys_desc{.fnc = backdrop, .scene = g.play, .name = "backdrop"});
  ecs_register(ctx, phase_render, sys_desc{.fnc = talk_hint, .scene = g.play, .name = "talk_hint"});
}
} // namespace

mod_desc play_module() { return mod_desc{.name = "plat.play", .setup = setup}; }

void register_prefabs(context &ctx) {
  prefab_register(ctx, {.name = "player", .build = build_player});
  prefab_register(ctx, {.name = "coin", .build = build_coin});
  prefab_register(ctx, {.name = "walker", .build = build_walker});
  prefab_register(ctx, {.name = "checkpoint", .build = build_checkpoint});
  prefab_register(ctx, {.name = "exit", .build = build_exit});
  prefab_register(ctx, {.name = "spikes", .build = build_spikes});
  prefab_register(ctx, {.name = "hazard", .build = build_hazard});
  prefab_register(ctx, {.name = "sign", .build = build_sign});
  prefab_register(ctx, {.name = "npc", .build = build_npc});
  prefab_register(ctx, {.name = "platform", .build = build_platform});
}

void play_enter(context &ctx) {
  g.coins = 0;
  g.coins_total = 0;
  g.dying = false;
  g.finished = false;
  g.paused = false;
  g.settings_open = false;
  g.player = entt::null;
  g.near_talk = entt::null;
  g.level = level_load(ctx, g.level_file.c_str());
  if (g.level.id == 0) {
    ui_toast(ctx, "Cannot load the level", {.kind = ui_toast_error});
    return;
  }
  // The camera: the world is 320 x 180 on a 640 x 360 screen.
  g.camera = camera_spawn(ctx, 2.0f);
  world(ctx).emplace<scene_owned>(g.camera, scene_owned{g.play});
  world(ctx).emplace<camera_follow>(g.camera, camera_follow{.target = g.player,
                                                            .offset = {0.0f, -20.0f},
                                                            .deadzone = {24.0f, 40.0f},
                                                            .smoothing = 0.1f,
                                                            .lookahead = {36.0f, 0.0f},
                                                            .bounds = level_bounds(ctx, g.level),
                                                            .pixel_snap = true});
  const char *music = level_properties(ctx, g.level)["music"].string_or("level");
  music_crossfade(ctx, std::string(music) == "title" ? g.m_title : g.m_level, 0.8f);
}

void play_exit(context &ctx) {
  time_set_paused(ctx, false);
  dialog_stop(ctx);
  g.player = entt::null;
}
} // namespace plat
