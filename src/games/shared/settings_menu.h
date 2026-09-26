// Settings screen shared by the sample games: volume per bus, fullscreen,
// language and key rebinding, saved to settings.json when closed.
#pragma once
#include <njin.h>
#include <span>

namespace shared {
// One rebindable action: shown as "label | key | pad button".
struct rebind_row {
  const char *label_key; // string table key, e.g. "action.jump"
  njin::action_handle action;
};

// Loads the Be Vietnam Pro font at the sizes the samples use and sets the UI
// and dialogue styles for a 640 x 360 screen.
void apply_style(njin::njin_ctx &ctx, const char *font_path);

// Draws the settings panel. Returns true on the frame the player leaves it
// (the Back button, Esc, or pad B); settings are saved then.
bool settings_panel(njin::njin_ctx &ctx, std::span<const rebind_row> rows);
} // namespace shared
