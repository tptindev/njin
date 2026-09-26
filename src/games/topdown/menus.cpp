// Title, HUD, pause, win and game over screens.
#include "../shared/settings_menu.h"
#include "game.h"
#include <cmath>
#include <cstdio>

namespace td {
namespace {
font_handle g_big{};

std::span<const shared::rebind_row> rows() {
  static shared::rebind_row r[4];
  r[0] = {"action.attack", g.attack};
  r[1] = {"action.dash", g.dash};
  r[2] = {"action.interact", g.interact};
  r[3] = {"action.pause", g.pause};
  return r;
}

void text_centered(njin_ctx &ctx, const char *text, f32 y, f32 size, rgba color, font_handle font) {
  const vec2 m = text_measure(ctx, text, size, font);
  const f32 x = (screen_size(ctx).x - m.x) * 0.5f;
  draw_text(ctx, text, {x + 2.0f, y + 2.0f}, size, {0.05f, 0.1f, 0.05f, 0.6f}, font);
  draw_text(ctx, text, {x, y}, size, color, font);
}

void start_run(njin_ctx &ctx) { scene_fade(ctx, g.play); }

void title_draw(njin_ctx &ctx) {
  draw_rect(ctx, rect{{0.0f, 0.0f}, screen_size(ctx)}, {0.23f, 0.49f, 0.27f, 1.0f});
  const f32 t = elapsed(ctx);
  text_centered(ctx, tr(ctx, "game.title"), 50.0f + std::sin(t * 2.0f) * 3.0f, 40.0f, {1.0f, 0.95f, 0.6f, 1.0f}, g_big);
  texture_draw_ex(ctx, g.sprites,
                  texture_draw_desc{.pos = {screen_size(ctx).x * 0.5f, 150.0f},
                                    .source = {{0.0f, 0.0f}, {16.0f, 16.0f}},
                                    .scale = {3.0f, 3.0f},
                                    .origin = {0.5f, 1.0f}});
  if (g.settings_open) {
    if (shared::settings_panel(ctx, rows()))
      g.settings_open = false;
    return;
  }
  ui_begin(ctx, {.id = "title", .anchor = {0.5f, 0.74f}, .width = 220.0f});
  if (ui_button(ctx, tr(ctx, "menu.play")))
    start_run(ctx);
  if (ui_button(ctx, tr(ctx, "menu.settings")))
    g.settings_open = true;
  if (ui_button(ctx, tr(ctx, "menu.quit")))
    njin_quit(ctx);
  ui_end(ctx);
}

void hud(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  const font_handle font = ui_style_get(ctx).font;
  i32 hp = 0;
  if (reg.valid(g.player))
    hp = reg.get<player_tag>(g.player).hp;
  for (i32 i = 0; i < 5; i++)
    texture_draw_ex(ctx, g.sprites,
                    texture_draw_desc{.pos = {10.0f + (f32)i * 30.0f, 8.0f},
                                      .source = cell_of(i < hp ? 18 : 19),
                                      .scale = {2.0f, 2.0f}});
  const std::string left = trf(ctx, "hud.slimes", {std::to_string(g.slimes_left)});
  const vec2 m = text_measure(ctx, left.c_str(), 16.0f, font);
  draw_text(ctx, left.c_str(), {screen_size(ctx).x - m.x - 14.0f, 14.0f}, 16.0f, colors::white, font);
  const std::string time = format_time(g.run_time);
  const vec2 tm = text_measure(ctx, time.c_str(), 16.0f, font);
  draw_text(ctx, time.c_str(), {screen_size(ctx).x - tm.x - 14.0f, 36.0f}, 16.0f, colors::white, font);

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

void win_draw(njin_ctx &ctx) {
  draw_rect(ctx, rect{{0.0f, 0.0f}, screen_size(ctx)}, {0.10f, 0.20f, 0.12f, 1.0f});
  text_centered(ctx, tr(ctx, "win.title"), 44.0f, 36.0f, {1.0f, 0.95f, 0.6f, 1.0f}, g_big);
  ui_begin(ctx, {.id = "win", .anchor = {0.5f, 0.62f}, .width = 280.0f});
  ui_label(ctx, tr(ctx, "win.text"));
  ui_label(ctx, trf(ctx, "win.time", {format_time(g.run_time)}).c_str());
  ui_label(ctx, trf(ctx, "win.hits", {std::to_string(g.hits_taken)}).c_str());
  ui_space(ctx, 6.0f);
  if (ui_button(ctx, tr(ctx, "menu.again")))
    start_run(ctx);
  if (ui_button(ctx, tr(ctx, "menu.to_title")))
    scene_fade(ctx, g.title);
  ui_end(ctx);
}

void over_draw(njin_ctx &ctx) {
  draw_rect(ctx, rect{{0.0f, 0.0f}, screen_size(ctx)}, {0.15f, 0.06f, 0.06f, 1.0f});
  text_centered(ctx, tr(ctx, "over.title"), 70.0f, 36.0f, {1.0f, 0.5f, 0.5f, 1.0f}, g_big);
  ui_begin(ctx, {.id = "over", .anchor = {0.5f, 0.62f}, .width = 260.0f});
  if (ui_button(ctx, tr(ctx, "menu.again")))
    start_run(ctx);
  if (ui_button(ctx, tr(ctx, "menu.to_title")))
    scene_fade(ctx, g.title);
  ui_end(ctx);
}

void setup(njin_ctx &ctx) {
  g_big = font_load(ctx, "assets/fonts/BeVietnamPro-Bold.ttf", 40);
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = title_draw, .scene = g.title, .name = "title_draw"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = hud, .scene = g.play, .name = "hud"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = win_draw, .scene = g.win, .name = "win_draw"});
  ecs_register(ctx, phase_post_render, sys_desc{.fnc = over_draw, .scene = g.over, .name = "over_draw"});
}
} // namespace

mod_desc menus_module() { return mod_desc{.name = "td.menus", .setup = setup}; }

void title_enter(njin_ctx &ctx) {
  g.settings_open = false;
  music_crossfade(ctx, g.m_forest, 1.0f);
}

void end_enter(njin_ctx &ctx) {
  music_crossfade(ctx, g.m_forest, 1.0f);
  ui_focus(ctx, tr(ctx, "menu.again"));
}

std::string format_time(f32 seconds) {
  const i32 total = (i32)seconds;
  char text[16];
  std::snprintf(text, sizeof text, "%d:%02d", total / 60, total % 60);
  return text;
}
} // namespace td
