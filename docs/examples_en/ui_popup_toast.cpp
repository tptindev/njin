#include <njin.h>

namespace {
bool quit_popup_open = false;
bool has_unsaved = true;

void save_game(njin::njin_ctx &ctx) {
  // ... write the file ...
  has_unsaved = false;
  // Toast: call it from anywhere, no ui_begin needed.
  njin::ui_toast(ctx, "Game saved", {.kind = njin::ui_toast_success});
}

void menus(njin::njin_ctx &ctx) {
  njin::ui_begin(ctx, {.id = "pause", .title = "Paused"});
  if (njin::ui_button(ctx, "Save"))
    save_game(ctx);
  if (njin::ui_button(ctx, "Quit"))
    quit_popup_open = true;
  njin::ui_end(ctx);

  // Popup: call it every frame, `open` is kept by the game. The menu above is still drawn,
  // but stays still (modal) until the popup closes.
  if (quit_popup_open) {
    const njin::i32 pick = njin::ui_popup(
        ctx,
        {.id = "quit",
         .title = "Quit game?",
         .message = has_unsaved ? "Unsaved progress will be lost." : "See you soon!",
         .buttons = {"Stay", "Save and quit", "Quit"},
         .default_button = 0, // the safe button is preselected
         .cancel_button = 0}, // Esc / Backspace / B means "Stay"
        quit_popup_open);
    if (pick == 1) {
      save_game(ctx);
      njin::njin_quit(ctx);
    } else if (pick == 2) {
      njin::njin_quit(ctx);
    }
  }
}

// A popup with arbitrary content: ui_popup_begin / ui_popup_end wrap ordinary widgets.
bool settings_open = false;
njin::f32 volume = 0.8f;

void settings_popup(njin::njin_ctx &ctx) {
  if (!settings_open)
    return;
  njin::ui_popup_begin(ctx, {.id = "settings", .title = "Settings", .width = 460});
  njin::ui_slider(ctx, "Volume", volume, 0.0f, 1.0f, 0.05f, true);
  if (njin::ui_button(ctx, "Done") || njin::ui_back(ctx)) // ui_back only reports to the popup
    settings_open = false;
  njin::ui_popup_end(ctx);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_post_render, menus);
  njin::ecs_register(ctx, njin::phase_post_render, settings_popup);
}
} // namespace

njin::mod_desc ui_popup_toast_module() { return {.name = "ui_popup_toast", .setup = setup}; }
