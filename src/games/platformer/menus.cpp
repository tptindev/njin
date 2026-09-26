// Title screen, level card, pause menu, HUD and the win screen.
#include "../shared/settings_menu.h"
#include "game.h"
#include <cmath>
#include <cstdio>

namespace plat {
namespace {
font_handle g_big{}; // the title font, loaded once

std::span<const shared::rebind_row> rows() {
  static shared::rebind_row r[4];
  r[0] = {"action.jump", g.jump};
  r[1] = {"action.down", g.down};
  r[2] = {"action.interact", g.interact};
  r[3] = {"action.pause", g.pause};
  return r;
}

void text_centered(njin_ctx &ctx, const char *text, f32 y, f32 size, rgba color, font_handle font) {
  const vec2 m = text_measure(ctx, text, size, font);
  const f32 x = (screen_size(ctx).x - m.x) * 0.5f;
  draw_text(ctx, text, {x + 2.0f, y + 2.0f}, size, {0.1f, 0.1f, 0.2f, 0.6f}, font);
  draw_text(ctx, text, {x, y}, size, color, font);
}

// --- title ---

void title_draw(njin_ctx &ctx) {
  const f32 t = elapsed(ctx);
  text_centered(ctx, tr(ctx, "game.title"), 60.0f + std::sin(t * 2.0f) * 3.0f, 32.0f,
                {1.0f, 0.95f, 0.6f, 1.0f}, g_big);
  // The hero, bouncing.
  texture_draw_ex(ctx, g.sprites,
                  texture_draw_desc{.pos = {screen_size(ctx).x * 0.5f, 140.0f - std::abs(std::sin(t * 3.0f)) * 12.0f},
                                    .source = {{0.0f, 0.0f}, {16.0f, 16.0f}},
                                    .scale = {3.0f, 3.0f},
                                    .origin = {0.5f, 1.0f}});
  if (g.settings_open) {
    if (shared::settings_panel(ctx, rows()))
      g.settings_open = false;
    return;
  }
  ui_begin(ctx, {.id = "title", .anchor = {0.5f, 0.72f}, .width = 220.0f});
  if (ui_button(ctx, tr(ctx, "menu.play"))) {
    g.level_file = "assets/level1.tmx";
    g.run_coins = g.run_total = g.deaths = 0;
    g.run_time = 0.0f;
    scene_fade(ctx, scene_find(ctx, "card"));
  }
  if (ui_button(ctx, tr(ctx, "menu.settings")))
    g.settings_open = true;
  if (ui_button(ctx, tr(ctx, "menu.quit")))
    njin_quit(ctx);
  ui_end(ctx);
}

void screen_backdrop(njin_ctx &ctx) { draw_backdrop(ctx, {elapsed(ctx) * 20.0f, 0.0f}); }

// --- level card: the level's name for a moment, then the level ---

void card_draw(njin_ctx &ctx) {
  const bool second = g.level_file.find("level2") != std::string::npos;
  const char *name = tr(ctx, second ? "level.2" : "level.1");
  draw_rect(ctx, rect{{0.0f, 0.0f}, screen_size(ctx)}, {0.08f, 0.09f, 0.14f, 1.0f});
  text_centered(ctx, name, 150.0f, 32.0f, colors::white, g_big);
}

// --- play: HUD and pause ---

void hud(njin_ctx &ctx) {
  // Coins, top left.
  texture_draw_ex(ctx, g.sprites,
                  texture_draw_desc{.pos = {12.0f, 10.0f},
                                    .source = {{0.0f, 32.0f}, {16.0f, 16.0f}},
                                    .scale = {2.0f, 2.0f}});
  const std::string coins = trf(ctx, "hud.coins", {std::to_string(g.coins), std::to_string(g.coins_total)});
  draw_text(ctx, coins.c_str(), {48.0f, 16.0f}, 16.0f, colors::white, ui_style_get(ctx).font);
  const std::string time = format_time(g.run_time);
  const vec2 m = text_measure(ctx, time.c_str(), 16.0f, ui_style_get(ctx).font);
  draw_text(ctx, time.c_str(), {screen_size(ctx).x - m.x - 14.0f, 16.0f}, 16.0f, colors::white,
            ui_style_get(ctx).font);

  // Pause menu.
  if (!g.paused && !dialog_active(ctx) && !scene_transitioning(ctx) && action_pressed(ctx, g.pause)) {
    g.paused = true;
    time_set_paused(ctx, true);
    ui_focus(ctx, tr(ctx, "menu.resume"));
    return;
  }
  if (!g.paused)
    return;
  if (g.settings_open) {
    draw_rect(ctx, rect{{0.0f, 0.0f}, screen_size(ctx)}, {0.0f, 0.0f, 0.0f, 0.5f});
    if (shared::settings_panel(ctx, rows()))
      g.settings_open = false;
    return;
  }
  bool resume = false;
  ui_popup_begin(ctx, {.id = "pause", .title = tr(ctx, "menu.paused"), .width = 240.0f});
  if (ui_button(ctx, tr(ctx, "menu.resume")) || ui_back(ctx))
    resume = true;
  if (ui_button(ctx, tr(ctx, "menu.settings")))
    g.settings_open = true;
  if (ui_button(ctx, tr(ctx, "menu.to_title"))) {
    resume = true;
    scene_fade(ctx, g.title);
  }
  ui_popup_end(ctx);
  if (resume) {
    g.paused = false;
    time_set_paused(ctx, false);
  }
}

// --- win ---

void win_draw(njin_ctx &ctx) {
  text_centered(ctx, tr(ctx, "win.title"), 50.0f, 32.0f, {1.0f, 0.95f, 0.6f, 1.0f}, g_big);
  ui_begin(ctx, {.id = "win", .anchor = {0.5f, 0.62f}, .width = 260.0f});
  ui_label(ctx, trf(ctx, "win.coins", {std::to_string(g.run_coins), std::to_string(g.run_total)}).c_str());
  ui_label(ctx, trf(ctx, "win.time", {format_time(g.run_time)}).c_str());
  ui_label(ctx, trf(ctx, "win.deaths", {std::to_string(g.deaths)}).c_str());
  ui_space(ctx, 6.0f);
  if (ui_button(ctx, tr(ctx, "menu.again"))) {
    g.level_file = "assets/level1.tmx";
    g.run_coins = g.run_total = g.deaths = 0;
    g.run_time = 0.0f;
    scene_fade(ctx, scene_find(ctx, "card"));
  }
  if (ui_button(ctx, tr(ctx, "menu.to_title")))
    scene_fade(ctx, g.title);
  ui_end(ctx);
}

void setup(njin_ctx &ctx) {
  // VT323 is drawn at multiples of 16: 32 for titles, 16 for the rest.
  g_big = font_load(ctx, "assets/fonts/VT323-Regular.ttf", 32, font_pixel);
  const scene_handle card = scene_find(ctx, "card");
  ecs_register(ctx, phase_pre_render, sys_desc{.fnc = screen_backdrop, .scene = g.title, .name = "screen_backdrop"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = title_draw, .scene = g.title, .name = "title_draw"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = card_draw, .scene = card, .name = "card_draw"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = hud, .scene = g.play, .name = "hud"});
  ecs_register(ctx, phase_pre_render, sys_desc{.fnc = screen_backdrop, .scene = g.win, .name = "screen_backdrop"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = win_draw, .scene = g.win, .name = "win_draw"});
}
} // namespace

mod_desc menus_module() { return mod_desc{.name = "plat.menus", .setup = setup}; }

void title_enter(njin_ctx &ctx) {
  g.settings_open = false;
  music_crossfade(ctx, g.m_title, 1.0f);
}

void card_scene_enter(njin_ctx &ctx) {
  timer_after(ctx, 1.1f, [](njin_ctx &c) { scene_fade(c, g.play); }, {.real_time = true});
}

void win_enter(njin_ctx &ctx) {
  music_crossfade(ctx, g.m_title, 1.0f);
  ui_focus(ctx, tr(ctx, "menu.again"));
}

void draw_backdrop(njin_ctx &ctx, vec2 camera_pos) {
  const rect view = camera_bounds(ctx);
  const f32 bottom = view.pos.y + view.size.y;
  // Clouds drift on their own.
  const f32 t = elapsed(ctx);
  for (i32 i = 0; i < 6; i++) {
    const f32 span = view.size.x + 120.0f;
    const f32 x = view.pos.x + std::fmod((f32)i * 97.0f + t * 6.0f - camera_pos.x * 0.1f + span * 4.0f, span) - 60.0f;
    const f32 y = view.pos.y + view.size.y * (0.12f + 0.07f * (f32)(i % 3));
    draw_circle(ctx, {x, y}, view.size.y * 0.05f, {1.0f, 1.0f, 1.0f, 0.8f});
    draw_circle(ctx, {x + view.size.y * 0.05f, y + 2.0f}, view.size.y * 0.04f, {1.0f, 1.0f, 1.0f, 0.8f});
  }
  // Two rows of hills, the far one slower.
  const struct {
    f32 factor, height, width;
    rgba color;
  } rows_[] = {{0.25f, 0.45f, 0.5f, {0.55f, 0.78f, 0.62f, 1.0f}},
               {0.5f, 0.3f, 0.35f, {0.36f, 0.65f, 0.45f, 1.0f}}};
  for (const auto &r : rows_) {
    const f32 w = view.size.x * r.width;
    const f32 shift = std::fmod(camera_pos.x * r.factor, w);
    for (f32 x = view.pos.x - w - shift; x < view.pos.x + view.size.x + w; x += w) {
      const vec2 peak{x + w * 0.5f, bottom - view.size.y * r.height};
      draw_triangle(ctx, {x - w * 0.2f, bottom}, peak, {x + w * 1.2f, bottom}, r.color);
    }
  }
}

std::string format_time(f32 seconds) {
  const i32 total = (i32)seconds;
  char text[16];
  std::snprintf(text, sizeof text, "%d:%02d", total / 60, total % 60);
  return text;
}
} // namespace plat
