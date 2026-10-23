#include <njin.h>

namespace {
njin::i32 level = njin::quality_high;
bool open_settings = true;

void startup(njin::context &ctx) {
  // On later runs, settings_load() loads back the limit set the player
  // chose (key "graphics"); the level shown in the menu is the game's own
  // data (key "game").
  njin::json_value game;
  if (njin::settings_load(ctx, "settings.json", &game)) {
    level = (njin::i32)game["quality"].f32_or(njin::quality_high);
    return;
  }
  // First run, no settings file yet: guess a level that fits the machine.
  level = njin::graphics_quality_suggest(ctx);
  njin::graphics_quality_set(ctx, njin::graphics_quality_preset((njin::quality_level)level));
}

void settings_menu(njin::context &ctx) {
  if (!open_settings)
    return;
  njin::ui_begin(ctx, {.id = "settings", .title = "Settings"});
  if (njin::ui_choice(ctx, "Graphics", level, {"Low", "Medium", "High"}))
    njin::graphics_quality_set(ctx, njin::graphics_quality_preset((njin::quality_level)level));
  // A ready-made level, with one extra field tweaked: draw the world at half resolution.
  if (njin::ui_button(ctx, "Ultra light")) {
    njin::graphics_quality q = njin::graphics_quality_preset(njin::quality_low);
    q.world_scale = 0.5f;
    njin::graphics_quality_set(ctx, q);
  }
  if (njin::ui_button(ctx, "Done")) {
    njin::json_value game = njin::json_value::make_object();
    game.set("quality", level);
    njin::settings_save(ctx, "settings.json", &game);
    open_settings = false;
  }
  njin::ui_end(ctx);
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "Graphics quality", .width = 960, .height = 540, .target_fps = 60});
  njin::ecs_register(*ctx, njin::phase_startup, startup);
  njin::ecs_register(*ctx, njin::phase_post_render, settings_menu);
  njin::run(*ctx);
  njin::destroy(ctx);
}
