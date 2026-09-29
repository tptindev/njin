#include <njin.h>

namespace {
enum class screen { main, settings, none };
screen open = screen::main;
bool fullscreen = false;
njin::f32 volume = 0.8f;
njin::i32 difficulty = 1;

void startup(njin::context &ctx) {
  // A font that covers Vietnamese text, and a size that scales with the window.
  njin::ui_style style = njin::ui_default_style();
  style.font = njin::font_load(ctx, "assets/fonts/Inter.ttf", 32);
  style.scale = njin::screen_size(ctx).y / 720.0f;
  njin::ui_style_set(ctx, style);
}

void menus(njin::context &ctx) {
  if (open == screen::main) {
    njin::ui_begin(ctx, {.id = "main", .title = "Game title"});
    if (njin::ui_button(ctx, "Play"))
      open = screen::none;
    if (njin::ui_button(ctx, "Settings"))
      open = screen::settings;
    if (njin::ui_button(ctx, "Quit"))
      njin::quit(ctx);
    njin::ui_end(ctx);
  } else if (open == screen::settings) {
    njin::ui_begin(ctx, {.id = "settings", .title = "Settings", .width = 480});
    if (njin::ui_toggle(ctx, "Fullscreen", fullscreen))
      njin::window_set_fullscreen(ctx, fullscreen);
    njin::ui_slider(ctx, "Volume", volume, 0.0f, 1.0f, 0.05f, true);
    njin::ui_choice(ctx, "Difficulty", difficulty, {"Easy", "Normal", "Hard"});
    njin::ui_row(ctx, 2);
    const bool done = njin::ui_button(ctx, "Done");
    const bool reset = njin::ui_button(ctx, "Defaults");
    if (reset) {
      volume = 0.8f;
      difficulty = 1;
    }
    // Esc / Backspace / the B button also go back.
    if (done || njin::ui_back(ctx))
      open = screen::main;
    njin::ui_end(ctx);
  }
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_post_render, menus);
}
} // namespace

njin::mod_desc ui_menu_module() { return {.name = "ui_menu", .setup = setup}; }
