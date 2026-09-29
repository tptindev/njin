#include <njin.h>
#include <string_view>

namespace {
njin::ui_layout menu;
bool in_game = false;

void startup(njin::context &ctx) {
  // The file saved by njin_ui_editor. If loading fails the game still runs, just without a menu.
  njin::ui_layout_load(ctx, "assets/ui/menu.ui.json", menu);
}

void draw_menu(njin::context &ctx) {
  if (in_game)
    return;
  // Draws every panel that is `visible` and every popup that is `open` in the file.
  njin::ui_draw_layout(ctx, menu, [&](const njin::ui_layout_event &ev) {
    // widget_id is a const char*: compare with string_view, not with ==.
    const std::string_view id = ev.widget_id;
    if (ev.kind == njin::ui_layout_event::button_clicked) {
      if (id == "btn_play")
        in_game = true;
      else if (id == "btn_settings")
        njin::ui_layout_set_bool(menu, "tog_fullscreen", false);
      else if (id == "btn_quit")
        njin::quit(ctx);
    }
  });
  // Read any widget value at any time, no callback needed.
  const njin::f32 volume = njin::ui_layout_get_float(menu, "sld_volume", 0.8f);
  (void)volume;
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_post_render, draw_menu);
}
} // namespace

njin::mod_desc ui_layout_module() { return {.name = "ui_layout", .setup = setup}; }
