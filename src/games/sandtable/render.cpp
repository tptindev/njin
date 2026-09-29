#include "render.h"
#include "audio.h"
#include "fog.h"
#include "levels.h"
#include "sim.h"
#include "sprites.h"
#include "weather.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sandtable {

namespace {

// Everything is drawn in a 640 x 360 image and scaled up by whole numbers.
// The world is drawn at one of camera_zooms, so a world pixel (px()) is a
// whole number of screen pixels or a whole fraction of one; sizes and
// positions are rounded to it so nothing shimmers.

font_handle pixel_font{};
texture_handle tiles_tex{};
// The pass over the world picture: shockwaves and light (assets/shaders/world.fs).
shader_handle sh_world{};

// Every eighth man carries a torch once it grows dark.
bool carries_torch(u32 index) { return index % 8 == 3; }

// A torch's flicker, 0.75 to 1, different for each torch.
f32 flicker(u32 index, f32 t) {
  return 0.85f + 0.1f * std::sin(t * 13.0f + static_cast<f32>(index) * 1.7f) +
         0.05f * std::sin(t * 31.0f + static_cast<f32>(index) * 0.3f);
}

// Hands the running shockwaves, the sky's light and the lights on the ground
// to the world's post shader, in screen pixels.
void update_post(context &ctx) {
  if (sh_world.id == 0)
    return;
  const f32 zoom = camera_zoom();
  const vec2 scr = screen_size(ctx);
  vec4 waves[16]{};
  i32 n = 0;
  for (const shockwave &w : state.shockwaves) {
    if (n == 16)
      break;
    const f32 k = clamp(w.time / w.duration, 0.0f, 1.0f);
    const f32 front = w.radius * (1.0f - (1.0f - k) * (1.0f - k)); // fast, then slowing
    const vec2 at = w2scr(ctx, w.pos);
    waves[n++] = vec4{at.x, at.y, front * zoom, w.strength * (1.0f - k)};
  }
  shader_set_vec2(ctx, sh_world, "resolution", scr);
  shader_set_i32(ctx, sh_world, "wave_count", n);
  if (n > 0)
    shader_set_vec4_array(ctx, sh_world, "waves", waves, static_cast<u32>(n));

  // Lights: blasts first, then torches or camp fires, those on screen only.
  constexpr i32 max_lights = 64;
  vec4 lights[max_lights]{};
  i32 count = 0;
  const f32 dark = darkness();
  const f32 t = elapsed(ctx);
  const auto add = [&](vec2 world_pos, f32 radius, f32 strength) {
    if (count == max_lights)
      return;
    const vec2 at = w2scr(ctx, world_pos);
    const f32 r = radius * zoom;
    if (at.x < -r || at.y < -r || at.x > scr.x + r || at.y > scr.y + r)
      return;
    lights[count++] = vec4{at.x, at.y, r, strength};
  };
  if (dark > 0.05f) {
    // Blasts: only each one's big flash, and never so many that the torches
    // are left no room.
    constexpr i32 max_blasts = 24;
    for (const fx_particle &p : state.particles)
      if (p.kind == fx_kind::fire && p.delay <= 0.0f && p.size >= 10.0f && count < max_blasts)
        add(p.pos, p.size * 6.0f, dark * clamp(p.life / p.max_life * 2.0f, 0.0f, 1.0f));
    if (state.screen == phase::deploy) {
      // The camps' fires by each chip.
      for (const board_chip &c : current_level().enemy)
        if (fog_visible(c.pos))
          add(c.pos, 90.0f, dark * flicker(static_cast<u32>(c.pos.x), t));
      for (const board_chip &c : state.board)
        add(c.pos, 90.0f, dark * flicker(static_cast<u32>(c.pos.x), t));
    } else if (dark > 0.3f) {
      // More torches than lights: take every so many, spread over the field.
      i32 torches = 0;
      for (u32 i = 0; i < state.soldiers.size(); ++i)
        torches += state.soldiers[i].alive && carries_torch(i) ? 1 : 0;
      const i32 room = std::max(1, max_lights - count);
      const i32 stride = std::max(1, (torches + room - 1) / room);
      i32 seen = 0;
      for (u32 i = 0; i < state.soldiers.size(); ++i) {
        const soldier &s = state.soldiers[i];
        if (!s.alive || !carries_torch(i) || seen++ % stride != 0)
          continue;
        if (s.owner != side::player && !fog_visible(s.pos))
          continue; // an enemy torch in the fog would give them away
        // One light for several torches reaches a little further, not as far as all of them.
        add(s.pos, 44.0f * std::min(2.0f, std::sqrt(static_cast<f32>(stride))), dark * flicker(i, t));
      }
    }
  }
  const vec3 sky = daylight();
  shader_set_vec3(ctx, sh_world, "ambient", sky);
  shader_set_i32(ctx, sh_world, "light_count", count);
  if (count > 0)
    shader_set_vec4_array(ctx, sh_world, "lights", lights, static_cast<u32>(count));
}
constexpr f32 font_size = 16.0f; // VT323's design size
ui_style hud_style{};

constexpr rgba col_panel = rgb(34, 28, 22, 240);
constexpr rgba col_panel_edge = rgb(140, 98, 58);
constexpr rgba col_button = rgb(70, 54, 38);
constexpr rgba col_button_hover = rgb(104, 78, 50);
constexpr rgba col_button_down = rgb(140, 104, 50);
constexpr rgba col_button_off = rgb(46, 38, 30);
constexpr rgba col_text_hint = rgb(176, 158, 128);

// World units per screen pixel at the current zoom.
f32 px() { return 1.0f / camera_zoom(); }

f32 snap(f32 v) {
  const f32 p = px();
  return std::floor(v / p + 0.5f) * p;
}
vec2 snap(vec2 v) { return {snap(v.x), snap(v.y)}; }

// A size in world units, rounded to whole screen pixels and at least one.
f32 snap_size(f32 v) { return std::max(1.0f, std::floor(v * camera_zoom() + 0.5f)) * px(); }

rgba side_color(side s) { return s == side::player ? col_player : col_enemy; }
rgba side_dark(side s) { return s == side::player ? col_player_dark : col_enemy_dark; }
rgba with_alpha(rgba c, f32 a) { return {c.r, c.g, c.b, c.a * a}; }

void text(context &ctx, const char *str, vec2 pos, rgba col, f32 size = font_size) {
  draw_text(ctx, str, {std::floor(pos.x), std::floor(pos.y)}, size, col, pixel_font);
}
vec2 measure(context &ctx, const char *str, f32 size = font_size) {
  return text_measure(ctx, str, size, pixel_font);
}
void text_centered(context &ctx, const char *str, vec2 center, rgba col, f32 size = font_size) {
  text(ctx, str, center - measure(ctx, str, size) * 0.5f, col, size);
}
// Text with a one-pixel dark shadow, for text over the table.
void text_shadow(context &ctx, const char *str, vec2 pos, rgba col, f32 size = font_size) {
  text(ctx, str, pos + vec2{1.0f, 1.0f}, rgb(20, 14, 10, static_cast<i32>(200 * col.a)), size);
  text(ctx, str, pos, col, size);
}

// A pixel square of `side` world units centred on `c`.
void dot(context &ctx, vec2 c, f32 side, rgba col) {
  const f32 s = snap_size(side);
  draw_rect(ctx, {snap(c - vec2{s, s} * 0.5f), {s, s}}, col);
}

// Formation slots per arm and tier, computed once.
const std::vector<vec2> &slots_of(arm a, i32 tier) {
  static std::vector<vec2> cache[arm_count][tier_count];
  static bool ready = false;
  if (!ready) {
    for (i32 k = 0; k < arm_count; ++k)
      for (i32 t = 0; t < tier_count; ++t)
        cache[k][t] = formation_slots(static_cast<arm>(k), t);
    ready = true;
  }
  return cache[static_cast<i32>(a)][tier];
}

// --- Sand table ---

// The terrain tiles in view (levels.h), each tile pixel 4 world units, and a
// faint survey grid every four tiles: it is still a sand table.
void draw_table(context &ctx) {
  const f32 p = px();
  draw_rect(ctx, {{-40.0f, -40.0f}, {world_width + 80.0f, world_height + 80.0f}}, col_frame);
  draw_rect_lines(ctx, {{-40.0f, -40.0f}, {world_width + 80.0f, world_height + 80.0f}}, 2.0f * p, col_frame_light);
  if (tiles_tex.id == 0)
    return;
  const vec2 lo = scr2w(ctx, {0.0f, 0.0f});
  const vec2 hi = scr2w(ctx, screen_size(ctx));
  const i32 x0 = std::max(0, static_cast<i32>(std::floor(lo.x / tile_world)));
  const i32 y0 = std::max(0, static_cast<i32>(std::floor(lo.y / tile_world)));
  const i32 x1 = std::min(tiles_x - 1, static_cast<i32>(std::floor(hi.x / tile_world)));
  const i32 y1 = std::min(tiles_y - 1, static_cast<i32>(std::floor(hi.y / tile_world)));
  const std::vector<tile_layers> &layers = terrain_tiles();
  constexpr f32 texel = tile_world / 8.0f;
  for (i32 y = y0; y <= y1; ++y)
    for (i32 x = x0; x <= x1; ++x)
      for (const i16 id : layers[static_cast<usize>(y * tiles_x + x)].layer) {
        if (id < 0)
          break;
        const rect src{{static_cast<f32>(id % tileset_columns) * 8.0f, static_cast<f32>(id / tileset_columns) * 8.0f},
                       {8.0f, 8.0f}};
        texture_draw_ex(ctx, tiles_tex,
                        texture_draw_desc{.pos = {static_cast<f32>(x) * tile_world, static_cast<f32>(y) * tile_world},
                                          .source = src,
                                          .scale = {texel, texel}});
      }
  const rgba grid = rgb(40, 30, 20, 40);
  for (i32 x = 4; x < tiles_x; x += 4)
    draw_rect(ctx, {{static_cast<f32>(x) * tile_world, 0.0f}, {p, world_height}}, grid);
  for (i32 y = 4; y < tiles_y; y += 4)
    draw_rect(ctx, {{0.0f, static_cast<f32>(y) * tile_world}, {world_width, p}}, grid);
}

void draw_zones(context &ctx) {
  const f32 a = state.screen == phase::deploy ? 1.0f : 0.35f;
  const f32 p = px();
  draw_rect(ctx, player_zone, with_alpha(rgb(205, 52, 44, 26), a));
  draw_rect_lines(ctx, player_zone, p, with_alpha(rgb(205, 52, 44, 140), a));
  draw_rect(ctx, enemy_zone, with_alpha(rgb(36, 110, 160, 26), a));
  draw_rect_lines(ctx, enemy_zone, p, with_alpha(rgb(36, 110, 160, 140), a));
}

// --- Deployment ---

// Where a chip's soldiers will stand, as faint pixels.
void draw_footprint(context &ctx, vec2 pos, arm a, i32 tier, side owner, f32 alpha) {
  const f32 flip = owner == side::player ? 1.0f : -1.0f;
  const rgba c = with_alpha(side_dark(owner), alpha);
  for (const vec2 &s : slots_of(a, tier))
    dot(ctx, pos + vec2{s.x * flip, s.y * flip}, 1.0f, c);
}

void draw_board_chip(context &ctx, const board_chip &c, bool hover, f32 alpha = 1.0f, bool bad = false) {
  const rgba tint = bad ? rgba{1.0f, 0.45f, 0.4f, alpha} : rgba{1.0f, 1.0f, 1.0f, alpha};
  draw_chip_sprite(ctx, snap(c.pos), c.type, c.tier, c.owner, chip_texel, tint);
  if (hover)
    draw_circle_lines(ctx, snap(c.pos), chip_radius(c.tier) + 3.0f * px(), px(), col_gold);
}

void draw_deploy(context &ctx) {
  const vec2 mouse = scr2w(ctx, mouse_pos(ctx));
  const bool over_ui = ui_mouse_over(ctx);
  const i32 hover_enemy = over_ui || !fog_visible(mouse) ? -1 : board_chip_at(mouse, side::enemy);
  const i32 hover_own = over_ui || state.held.active ? -1 : board_chip_at(mouse, side::player);

  const auto &foes = current_level().enemy;
  for (const board_chip &c : foes)
    draw_footprint(ctx, c.pos, c.type, c.tier, c.owner, 0.5f);
  for (const board_chip &c : state.board)
    draw_footprint(ctx, c.pos, c.type, c.tier, c.owner, 0.5f);
  for (usize i = 0; i < foes.size(); ++i)
    draw_board_chip(ctx, foes[i], static_cast<i32>(i) == hover_enemy);
  for (usize i = 0; i < state.board.size(); ++i)
    draw_board_chip(ctx, state.board[i], static_cast<i32>(i) == hover_own);

  if (state.held.active && !over_ui) {
    const bool ok = placement_error(mouse) == nullptr;
    draw_footprint(ctx, mouse, state.held.type, state.held.tier, side::player, ok ? 0.8f : 0.4f);
    draw_board_chip(ctx, {state.held.type, state.held.tier, side::player, mouse}, true, ok ? 0.9f : 0.6f, !ok);
  }
}

// --- Battle ---

// Sheet pixels per world unit for a figure: 1, or 2 for the heavy figures of
// big chips so they read as bigger.
f32 figure_scale(f32 radius) { return radius > 6.5f ? 2.0f : 1.0f; }

soldier_frame frame_of(const soldier &s) {
  if (s.fighting) {
    const f32 phase = s.cooldown / spec(s.type).interval; // 1 just after a blow, 0 ready for the next
    if (s.type == arm::artillery)
      return phase > 0.9f ? soldier_frame::attack1 : phase > 0.75f ? soldier_frame::attack2 : soldier_frame::idle;
    return phase > 0.75f ? soldier_frame::attack2 : soldier_frame::attack1;
  }
  if (s.moving)
    return static_cast<i32>(s.anim * 6.0f) % 2 == 0 ? soldier_frame::walk1 : soldier_frame::walk2;
  return soldier_frame::idle;
}

void draw_soldiers(context &ctx) {
  static std::vector<u32> order;
  order.clear();
  for (u32 i = 0; i < state.soldiers.size(); ++i)
    if (state.soldiers[i].alive)
      order.push_back(i);
  // Back to front, so the ones lower on the table stand in front.
  std::sort(order.begin(), order.end(),
            [](u32 a, u32 b) { return state.soldiers[a].pos.y < state.soldiers[b].pos.y; });

  const bool sprites = camera_zoom() >= 1.0f;
  const bool torches = darkness() > 0.3f;
  const f32 t = elapsed(ctx);
  for (u32 i : order) {
    const soldier &s = state.soldiers[i];
    const bool flash = s.flash > 0.0f;
    if (!sprites) {
      // Zoomed out: each figure is a block of colour, a pixel or two.
      dot(ctx, s.pos, s.radius * 1.4f, flash ? col_white : side_color(s.owner));
      continue;
    }
    const f32 scale = figure_scale(s.radius);
    const sprite_variant v = flash ? sprite_variant::flash
                                   : (s.owner == side::player ? sprite_variant::player : sprite_variant::enemy);
    draw_soldier_sprite(ctx, snap(s.pos + vec2{0.0f, 4.0f * scale}), s.type, v, frame_of(s), s.facing.x < 0.0f,
                        scale);
    // A torch held up in the leading hand, its flame flickering.
    if (torches && carries_torch(i)) {
      const f32 side_x = (s.facing.x < 0.0f ? -3.0f : 3.0f) * scale;
      const vec2 head = s.pos + vec2{side_x, -5.0f * scale};
      const bool high = flicker(i, t) > 0.85f;
      dot(ctx, head + vec2{0.0f, scale}, scale, rgb(120, 80, 40));
      dot(ctx, head, scale, high ? rgb(255, 236, 140) : rgb(255, 170, 60));
      if (high)
        dot(ctx, head - vec2{0.0f, scale}, scale, rgb(255, 120, 40));
    }
  }
}

void draw_battle(context &ctx) {
  for (const scorch &sc : state.scorches) {
    draw_circle(ctx, snap(sc.pos), sc.radius, rgb(120, 96, 66, 110));
    draw_circle(ctx, snap(sc.pos), sc.radius * 0.55f, rgb(70, 56, 40, 110));
  }
  const bool sprites = camera_zoom() >= 1.0f;
  for (const corpse &c : state.corpses) {
    if (!sprites) {
      dot(ctx, c.pos, c.radius * 1.2f, with_alpha(side_dark(c.owner), 0.6f));
      continue;
    }
    const f32 scale = figure_scale(c.radius / 0.85f);
    const sprite_variant v = c.owner == side::player ? sprite_variant::player : sprite_variant::enemy;
    draw_soldier_sprite(ctx, snap(c.pos + vec2{0.0f, 4.0f * scale}), c.type, v, soldier_frame::dead,
                        c.facing.x < 0.0f, scale, rgba{0.7f, 0.66f, 0.62f, 0.85f});
  }

  draw_soldiers(ctx);

  for (const projectile &p : state.projectiles) {
    const f32 lift = std::sin(p.t * pi) * p.arc;
    const vec2 ground = lerp(p.from, p.to, p.t);
    const vec2 at = ground - vec2{0.0f, lift};
    if (p.source == arm::artillery) {
      dot(ctx, ground, 3.0f, rgb(60, 46, 34, 90));
      dot(ctx, at, 4.0f, rgb(36, 34, 34));
    } else {
      // An arrow: two pixels along its flight, head and fletching.
      const f32 t2 = std::min(1.0f, p.t + 0.02f);
      const vec2 next = lerp(p.from, p.to, t2) - vec2{0.0f, std::sin(t2 * pi) * p.arc};
      const vec2 dir = length(next - at) > 0.001f ? normalize(next - at) : normalize(p.to - p.from);
      dot(ctx, at, 1.0f, rgb(60, 45, 30));
      dot(ctx, at - dir * 3.0f * std::max(1.0f, px()), 1.0f, rgb(230, 220, 200));
    }
  }
}

// Colour of a particle at `k` = life left, 1 new to 0 gone.
rgba particle_color(const fx_particle &p, f32 k) {
  switch (p.kind) {
  case fx_kind::fire:
    if (k > 0.8f)
      return rgb(255, 250, 220);
    if (k > 0.55f)
      return rgb(255, 214, 90);
    if (k > 0.3f)
      return rgb(240, 130, 40);
    return rgb(170, 60, 30, 220);
  case fx_kind::smoke:
    return with_alpha(p.color, std::min(1.0f, k * 1.6f) * 0.6f);
  case fx_kind::dust:
    return with_alpha(p.color, k * 0.8f);
  default:
    return with_alpha(p.color, std::min(1.0f, k * 2.0f));
  }
}

void draw_fx(context &ctx) {
  // Smoke and dust first, so fire and sparks burn through them.
  for (i32 pass = 0; pass < 2; ++pass) {
    for (const fx_particle &p : state.particles) {
      if (p.delay > 0.0f)
        continue;
      const bool soft = p.kind == fx_kind::smoke || p.kind == fx_kind::dust;
      if (soft != (pass == 0))
        continue;
      const f32 k = clamp(p.life / std::max(0.001f, p.max_life), 0.0f, 1.0f);
      const rgba col = particle_color(p, k);
      switch (p.kind) {
      case fx_kind::ring: {
        // A shockwave: fast at first, slowing as it spreads; a bright front
        // two pixels thick with a faint band behind it.
        const f32 r = std::max(px(), p.size * (1.0f - k * k));
        draw_circle_lines(ctx, snap(p.pos), r, 2.0f * px(), col);
        if (r > 4.0f * px())
          draw_circle_lines(ctx, snap(p.pos), r - 3.0f * px(), px(), with_alpha(col, 0.35f));
        break;
      }
      case fx_kind::fire:
        dot(ctx, p.pos, p.size * (0.4f + 0.6f * k), col);
        break;
      default:
        dot(ctx, p.pos, p.size, col);
        break;
      }
    }
  }
}

// --- Screen overlays: text over the table, drawn in screen pixels ---

void draw_overlays(context &ctx) {
  if (state.screen == phase::deploy) {
    // At the zone's corner, but never off the left or top of the screen.
    const auto corner = [&](const rect &z) {
      const vec2 p = w2scr(ctx, z.pos) + vec2{4.0f, 2.0f};
      return vec2{std::max(p.x, 4.0f), std::max(p.y, 20.0f)};
    };
    text_shadow(ctx, "VÙNG TẬP KẾT QUÂN TA", corner(player_zone), rgb(240, 150, 130));
    text_shadow(ctx, "QUÂN ĐỊCH", corner(enemy_zone), rgb(150, 200, 240));
    return;
  }
  // A flag over each block: tier colour, arm tag, and how many are left.
  for (const group &g : state.groups) {
    if (g.alive == 0 || (g.owner != side::player && !fog_visible(g.centroid)))
      continue;
    const vec2 base = w2scr(ctx, g.centroid);
    const vec2 top{std::floor(base.x), std::floor(base.y) - 22.0f};
    draw_rect(ctx, {top, {1.0f, 18.0f}}, rgb(60, 45, 30));
    const tier_spec &ts = tiers[g.tier];
    const rect flag{top + vec2{1.0f, 0.0f}, {17.0f, 11.0f}};
    draw_rect(ctx, flag, ts.color);
    draw_rect_lines(ctx, flag, 1.0f, side_color(g.owner));
    // The arm's symbol, as on its chip, dark or light to stand out on the tier colour.
    const bool dark_flag = g.tier == 3 || g.tier == 4;
    draw_arm_symbol(ctx, flag.pos + vec2{6.0f, 3.0f}, g.type, 1.0f, dark_flag ? col_white : rgb(30, 26, 22));
    const f32 left = static_cast<f32>(g.alive) / static_cast<f32>(std::max(1, g.figures));
    draw_rect(ctx, {flag.pos + vec2{0.0f, 12.0f}, {17.0f, 2.0f}}, rgb(20, 14, 10, 160));
    draw_rect(ctx, {flag.pos + vec2{0.0f, 12.0f}, {std::ceil(17.0f * left), 2.0f}}, side_color(g.owner));
  }
  for (const popup_text &p : state.popups) {
    const f32 a = clamp(p.timer / std::max(0.001f, p.max_time), 0.0f, 1.0f);
    const vec2 at = w2scr(ctx, p.pos);
    text_centered(ctx, p.label.c_str(), at + vec2{2.0f, 2.0f}, rgb(20, 14, 10, static_cast<i32>(200 * a)), 32.0f);
    text_centered(ctx, p.label.c_str(), at, with_alpha(p.color, a), 32.0f);
  }
}

// --- HUD on njin UI, square and flat ---

ui_skin flat(rgba color, rgba edge = rgb(20, 14, 10)) {
  return ui_skin{.color = color, .roundness = 0.0f, .outline = edge, .outline_width = 1.0f};
}

ui_style make_hud_style() {
  ui_style s = ui_default_style();
  s.font = pixel_font;
  s.font_size = font_size;
  s.padding = 5.0f;
  s.spacing = 2.0f;
  s.widget_height = 16.0f;
  s.width = 200.0f;
  s.toast_width = 220.0f;
  s.toast_margin = {6.0f, 6.0f};
  s.dim = rgb(0, 0, 0, 150);

  s.panel.normal = flat(col_panel, col_panel_edge);
  s.panel.text = col_gold;
  s.label.text = s.label.text_focused = col_white;

  s.button.normal = flat(col_button);
  s.button.focused = flat(col_button_hover, col_gold);
  s.button.pressed = flat(col_button_down, col_gold);
  s.button.disabled = flat(col_button_off, rgb(30, 24, 18));
  s.button.text = col_white;
  s.button.text_focused = col_gold_light;
  s.button.text_disabled = rgb(110, 100, 86);

  const ui_skin slot = flat(rgb(20, 16, 12));
  s.track.normal = s.track.focused = s.track.pressed = s.track.disabled = slot;
  s.track.text = s.track.text_focused = col_white;
  const ui_skin fill{.color = col_good};
  s.fill.normal = s.fill.focused = s.fill.pressed = s.fill.disabled = fill;
  for (ui_look *look : {&s.panel, &s.button, &s.track, &s.fill, &s.knob, &s.toast})
    for (ui_skin *skin : {&look->normal, &look->focused, &look->pressed, &look->disabled})
      skin->roundness = 0.0f;

  s.sound_move = {};
  s.sound_accept = audio_sound(sfx_type::click);
  return s;
}

void hud_label(context &ctx, const char *str, rgba color) {
  ui_style s = ui_style_get(ctx);
  const rgba old = s.label.text;
  s.label.text = color;
  ui_style_set(ctx, s);
  ui_label(ctx, str);
  s.label.text = old;
  ui_style_set(ctx, s);
}

void hud_wrapped(context &ctx, const char *str, rgba color, f32 width) {
  for (const std::string &line : text_wrap(ctx, str, font_size, width, pixel_font))
    hud_label(ctx, line.c_str(), color);
}

void hud_bar(context &ctx, f32 value, const char *str, rgba color) {
  ui_style s = ui_style_get(ctx);
  const ui_look old = s.fill;
  s.fill.normal.color = color;
  ui_style_set(ctx, s);
  ui_progress(ctx, value, str);
  s.fill = old;
  ui_style_set(ctx, s);
}

void format_men(char *buf, usize n, f32 men) {
  if (men >= 10000.0f)
    std::snprintf(buf, n, "%.1fK", men / 1000.0f);
  else
    std::snprintf(buf, n, "%.0f", men);
}

void top_bar(context &ctx) {
  const vec2 scr = screen_size(ctx);
  const level_def &lvl = current_level();
  ui_begin(ctx, {.id = "top_bar", .anchor = {0.5f, 0.0f}, .pivot = {0.5f, 0.0f}, .width = scr.x, .navigable = false});
  char title[96];
  std::snprintf(title, sizeof(title), "%d/%d %s", state.level + 1, static_cast<i32>(levels().size()), lvl.name);

  if (state.screen == phase::deploy) {
    ui_row(ctx, 3);
    hud_label(ctx, title, col_gold);
    char gold[64];
    std::snprintf(gold, sizeof(gold), "Lương %d/%d · Cờ %d/%d", gold_left(), lvl.budget, chips_on_board(side::player),
                  lvl.max_chips);
    hud_label(ctx, gold, gold_left() > 0 ? col_gold_light : col_muted);
    char cap[48];
    std::snprintf(cap, sizeof(cap), "%02d:00 · Tối đa: %s", static_cast<i32>(state.hour), tiers[lvl.max_tier].name);
    hud_label(ctx, cap, col_white);
  } else {
    ui_row(ctx, 4);
    hud_label(ctx, title, col_gold);
    char a[32], b[32], txt[64];
    format_men(a, sizeof(a), state.men_now[0]);
    format_men(b, sizeof(b), state.men_start[0]);
    std::snprintf(txt, sizeof(txt), "Ta %s/%s", a, b);
    hud_bar(ctx, state.men_now[0] / state.men_start[0], txt, col_player);
    format_men(a, sizeof(a), state.men_now[1]);
    format_men(b, sizeof(b), state.men_start[1]);
    std::snprintf(txt, sizeof(txt), "Địch %s/%s", a, b);
    hud_bar(ctx, state.men_now[1] / state.men_start[1], txt, col_enemy);
    const i32 left = static_cast<i32>(std::max(0.0f, battle_time_limit - state.battle_time));
    std::snprintf(txt, sizeof(txt), "%02d:%02d · còn %d:%02d", static_cast<i32>(state.hour),
                  static_cast<i32>(std::fmod(state.hour, 1.0f) * 60.0f), left / 60, left % 60);
    hud_label(ctx, txt, left < 30 ? col_warn : col_white);
  }
  ui_end(ctx);
}

void shop_panel(context &ctx) {
  const level_def &lvl = current_level();
  constexpr f32 width = 180.0f;
  ui_begin(ctx, {.id = "shop", .title = "QUÂN NHU", .anchor = {0.0f, 0.0f}, .pivot = {0.0f, 0.0f},
                 .offset = {4.0f, 30.0f}, .width = width, .navigable = false});
  ui_row(ctx, arm_count);
  for (i32 k = 0; k < arm_count; ++k) {
    char id[32];
    std::snprintf(id, sizeof(id), "%s%s##arm%d", k == state.shop_arm ? ">" : "", arms[k].tag, k);
    if (ui_button(ctx, id))
      state.shop_arm = k;
  }
  const arm_spec &sp = arms[state.shop_arm];
  hud_label(ctx, sp.name, col_gold_light);
  hud_wrapped(ctx, sp.desc, rgb(200, 190, 170), width - 10.0f);

  // Tiers, at most four to a row.
  state.shop_tier = std::min(state.shop_tier, lvl.max_tier);
  const i32 count = lvl.max_tier + 1;
  for (i32 first = 0; first < count; first += 4) {
    ui_row(ctx, std::min(4, count - first));
    for (i32 t = first; t < std::min(count, first + 4); ++t) {
      char id[32];
      std::snprintf(id, sizeof(id), "%s%s##tier%d", t == state.shop_tier ? ">" : "", tiers[t].value, t);
      if (ui_button(ctx, id))
        state.shop_tier = t;
    }
  }
  const arm a = static_cast<arm>(state.shop_arm);
  const i32 cost = chip_cost(a, state.shop_tier);
  char info[64];
  std::snprintf(info, sizeof(info), "%s: %d lính", tiers[state.shop_tier].name, tiers[state.shop_tier].men);
  hud_label(ctx, info, col_white);
  const rect info_rect = ui_last_rect(ctx);
  char buy[48];
  std::snprintf(buy, sizeof(buy), "MUA (%d lương)", cost);
  const bool no_water = sails(a) && !current_level().river;
  if (no_water)
    hud_label(ctx, "Màn này không có sông.", col_bad);
  if (ui_button(ctx, buy, cost <= gold_left() && !no_water))
    shop_buy(ctx, a, state.shop_tier);
  ui_end(ctx);

  draw_chip_sprite(ctx, {std::floor(info_rect.pos.x + info_rect.size.x - 10.0f), std::floor(info_rect.pos.y + 8.0f)},
                   a, state.shop_tier, side::player, 1.0f);
}

void reserve_panel(context &ctx) {
  const level_def &lvl = current_level();
  constexpr f32 width = 180.0f;
  struct chip_row {
    rect r;
    arm a;
    i32 t;
  };
  std::vector<chip_row> rows;

  ui_begin(ctx, {.id = "reserve", .title = "DỰ BỊ", .anchor = {1.0f, 0.0f}, .pivot = {1.0f, 0.0f},
                 .offset = {-4.0f, 30.0f}, .width = width, .navigable = false});
  bool any = false;
  for (i32 k = 0; k < arm_count; ++k) {
    for (i32 t = tier_count - 1; t >= 0; --t) {
      const i32 n = state.reserve[k][t];
      if (n <= 0)
        continue;
      any = true;
      const arm a = static_cast<arm>(k);
      char line[64];
      std::snprintf(line, sizeof(line), "   %s %s x%d", arms[k].tag, tiers[t].value, n);
      hud_label(ctx, line, col_white);
      rows.push_back({ui_last_rect(ctx), a, t});
      ui_row(ctx, 4);
      char id[4][32];
      std::snprintf(id[0], 32, "Đặt##p%d_%d", k, t);
      std::snprintf(id[1], 32, "Gộp##m%d_%d", k, t);
      std::snprintf(id[2], 32, "Tách##s%d_%d", k, t);
      std::snprintf(id[3], 32, "Bán##b%d_%d", k, t);
      if (ui_button(ctx, id[0]))
        hold_from_reserve(ctx, a, t);
      if (ui_button(ctx, id[1], n >= 3 && t + 1 <= lvl.max_tier))
        reserve_merge(ctx, a, t);
      if (ui_button(ctx, id[2], t > 0))
        reserve_split(ctx, a, t);
      if (ui_button(ctx, id[3]))
        reserve_sell(ctx, a, t);
    }
  }
  if (!any)
    hud_wrapped(ctx, "Trống. Mua ở QUÂN NHU, rồi bấm Đặt và nhấp lên vùng tập kết.", col_text_hint, width - 10.0f);
  hud_wrapped(ctx, "Trái: đặt/nhấc cờ. Phải: trả về. Shift: đặt tiếp. Tab: ẩn bảng. Lăn: zoom.", col_text_hint,
              width - 10.0f);
  ui_end(ctx);

  for (const chip_row &r : rows)
    draw_chip_sprite(ctx, {std::floor(r.r.pos.x + 8.0f), std::floor(r.r.pos.y + 8.0f)}, r.a, r.t, side::player,
                     1.0f);
}

// Only the buttons, so it covers as little of the deployment zone as it can.
void deploy_bar(context &ctx) {
  ui_begin(ctx, {.id = "deploy_bar", .anchor = {0.5f, 1.0f}, .pivot = {0.5f, 1.0f}, .offset = {0.0f, -2.0f},
                 .width = 360.0f, .navigable = false});
  ui_row(ctx, 4);
  if (ui_button(ctx, "< Màn trước", state.level > 0))
    load_level(ctx, state.level - 1);
  if (ui_button(ctx, "Dọn sa bàn", !state.board.empty()))
    clear_board(ctx);
  if (ui_button(ctx, "XUẤT QUÂN", !state.board.empty()))
    start_battle(ctx);
  if (ui_button(ctx, "Màn sau >", state.level < state.unlocked))
    load_level(ctx, state.level + 1);
  ui_end(ctx);
}

void battle_bar(context &ctx) {
  ui_begin(ctx, {.id = "battle_bar", .anchor = {0.5f, 1.0f}, .pivot = {0.5f, 1.0f}, .offset = {0.0f, -4.0f},
                 .width = 250.0f, .navigable = false});
  ui_row(ctx, 4);
  if (ui_button(ctx, time_paused(ctx) ? "Chạy" : "Dừng"))
    time_set_paused(ctx, !time_paused(ctx));
  const char *labels[3] = {"x1", "x2", "x4"};
  for (i32 i = 0; i < 3; ++i) {
    char id[16];
    std::snprintf(id, sizeof(id), "%s%s", i == state.speed_index ? ">" : "", labels[i]);
    if (ui_button(ctx, id))
      set_speed(ctx, i);
  }
  hud_label(ctx, "Space: dừng · 1/2/3: tốc độ · Lăn: zoom", col_text_hint);
  ui_end(ctx);
}

// A small framed box of text lines at the mouse, kept on screen.
void tooltip(context &ctx, const char *const *lines, i32 n, rgba edge) {
  f32 w = 0.0f;
  for (i32 i = 0; i < n; ++i)
    w = std::max(w, measure(ctx, lines[i]).x);
  const vec2 scr = screen_size(ctx);
  vec2 at = mouse_pos(ctx) + vec2{10.0f, 8.0f};
  const vec2 size{std::floor(w) + 8.0f, 14.0f * static_cast<f32>(n) + 6.0f};
  at.x = std::min(at.x, scr.x - size.x - 2.0f);
  at.y = std::min(at.y, scr.y - size.y - 2.0f);
  at = {std::floor(at.x), std::floor(at.y)};
  draw_rect(ctx, {at, size}, col_panel);
  draw_rect_lines(ctx, {at, size}, 1.0f, edge);
  for (i32 i = 0; i < n; ++i)
    text(ctx, lines[i], at + vec2{4.0f, 14.0f * static_cast<f32>(i)}, i == 0 ? col_gold_light : col_white);
}

void chip_tooltip(context &ctx) {
  if (ui_mouse_over(ctx) || state.held.active)
    return;
  const vec2 mouse = scr2w(ctx, mouse_pos(ctx));
  if (!fog_visible(mouse))
    return;
  const board_chip *c = nullptr;
  const i32 e = board_chip_at(mouse, side::enemy);
  if (e >= 0) {
    c = &current_level().enemy[static_cast<usize>(e)];
  } else {
    const i32 p = board_chip_at(mouse, side::player);
    if (p >= 0)
      c = &state.board[static_cast<usize>(p)];
  }
  if (!c) {
    const terrain t = terrain_at(mouse);
    if (t == terrain::plain || mouse.x < 0.0f || mouse.y < 0.0f || mouse.x >= world_width || mouse.y >= world_height)
      return;
    static const char *what[] = {"", "Không đi qua được, phải đi vòng.", "Không đi qua được, phải đi vòng.",
                                  "Không lội được. Tìm bến cạn.", "Lội được, chậm.", "Chậm. Che nửa sát thương tên.",
                                  "Chỗ lội qua sông."};
    const char *lines[2] = {terrain_name(t), what[static_cast<i32>(t)]};
    tooltip(ctx, lines, 2, col_panel_edge);
    return;
  }
  char l1[96], l2[96];
  std::snprintf(l1, sizeof(l1), "%s %s · %s", c->owner == side::player ? "Ta:" : "Địch:", spec(c->type).name,
                tiers[c->tier].name);
  std::snprintf(l2, sizeof(l2), "%d lính · %d lương", tiers[c->tier].men, chip_cost(c->type, c->tier));
  const char *lines[2] = {l1, l2};
  tooltip(ctx, lines, 2, c->owner == side::player ? col_player : col_enemy_light);
}

void held_hint(context &ctx) {
  if (!state.held.active || ui_mouse_over(ctx))
    return;
  const char *err = placement_error(scr2w(ctx, mouse_pos(ctx)));
  if (!err)
    return;
  const char *lines[1] = {err};
  tooltip(ctx, lines, 1, col_bad);
}

void result_popup(context &ctx) {
  const bool won = state.won;
  ui_style result = hud_style;
  result.panel.text = won ? col_gold : col_bad;
  result.panel.normal.outline = won ? col_gold : col_bad;
  ui_style_set(ctx, result);

  ui_popup_begin(ctx, {.id = "result", .title = won ? "ĐẠI THẮNG" : "THẤT TRẬN", .width = 260.0f});
  hud_label(ctx, won ? "Quân địch tan vỡ." : "Quân ta vỡ trận.", col_white);
  char a[32], b[32], line[96];
  format_men(a, sizeof(a), state.men_start[0] - state.men_now[0]);
  format_men(b, sizeof(b), state.men_start[0]);
  std::snprintf(line, sizeof(line), "Ta mất: %s/%s lính", a, b);
  hud_label(ctx, line, rgb(240, 170, 160));
  format_men(a, sizeof(a), state.men_start[1] - state.men_now[1]);
  format_men(b, sizeof(b), state.men_start[1]);
  std::snprintf(line, sizeof(line), "Địch mất: %s/%s lính", a, b);
  hud_label(ctx, line, rgb(160, 200, 240));
  const i32 secs = static_cast<i32>(state.battle_time);
  std::snprintf(line, sizeof(line), "Giao chiến: %d:%02d", secs / 60, secs % 60);
  hud_label(ctx, line, col_white);
  const bool has_next = won && state.level + 1 < static_cast<i32>(levels().size());
  if (has_next && ui_button(ctx, "MÀN TIẾP THEO"))
    state.next_requested = true;
  if (ui_button(ctx, "BÀY LẠI TRẬN"))
    state.restart_requested = true;
  ui_popup_end(ctx);
  ui_style_set(ctx, hud_style);
}

} // namespace

void render_init(context &ctx) {
  pixel_font = font_load(ctx, "assets/fonts/VT323-Regular.ttf", static_cast<i32>(font_size), font_pixel);
  tiles_tex = texture_load(ctx, "assets/tiles.png");
  sh_world = shader_load(ctx, nullptr, "assets/shaders/world.fs");
  if (sh_world.id != 0)
    camera_set_post_shader(ctx, sh_world);
  if (tiles_tex.id != 0)
    texture_set_filter(ctx, tiles_tex, filter_nearest);
  hud_style = make_hud_style();
}

void render_cleanup(context &ctx) {
  if (pixel_font.id != 0) {
    font_unload(ctx, pixel_font);
    pixel_font = {};
  }
  if (tiles_tex.id != 0) {
    texture_unload(ctx, tiles_tex);
    tiles_tex = {};
  }
}

void render_world(context &ctx) {
  fog_update(ctx);
  update_post(ctx);
  draw_table(ctx);
  draw_zones(ctx);
  if (state.screen == phase::deploy)
    draw_deploy(ctx);
  else
    draw_battle(ctx);
  draw_fx(ctx);
  draw_clouds(ctx);
  fog_draw(ctx);
}

void render_ui(context &ctx) {
  ui_style_set(ctx, hud_style);
  draw_rain(ctx);
  draw_overlays(ctx);
  top_bar(ctx);
  if (state.screen == phase::deploy) {
    if (!state.hide_panels) {
      shop_panel(ctx);
      reserve_panel(ctx);
    }
    deploy_bar(ctx);
    chip_tooltip(ctx);
    held_hint(ctx);
  } else {
    battle_bar(ctx);
    if (state.screen == phase::result)
      result_popup(ctx);
  }
}

} // namespace sandtable
