#pragma once
#include "njin_json.h"

namespace njin {
struct context;

/// @addtogroup grp_settings
/// @{

/// The player's settings as JSON: volume and mute per channel, bound keys,
/// fullscreen, vertical sync, language.
/// @code{.json}
/// { "audio": { "master": 0.8, "music": 0.5, "sfx": 1, "ui": 1, "voice": 1,
///              "muted": ["music"] },
///   "input": { "actions": {...}, "axes": {...} },
///   "fullscreen": false, "vsync": true, "language": "vi" }
/// @endcode
/// @param ctx Engine context.
/// @return A JSON object.
json_value settings_to_json(const context &ctx);

/// Applies settings from the JSON of settings_to_json(). Any missing part is left
/// unchanged; a language that is not loaded is ignored.
/// @param ctx Engine context.
/// @param json Data.
void settings_apply(context &ctx, const json_value &json);

/// Saves the settings to a file in the game's save folder (save_path()), together with the
/// game's own data if any (difficulty, brightness...), under the key `"game"`.
/// @code
/// njin::settings_save(ctx); // when leaving the settings menu
/// @endcode
/// @param ctx Engine context.
/// @param file File name.
/// @param game The game's own data, or null.
/// @return `true` if it was written.
bool settings_save(const context &ctx, const char *file = "settings.json",
                   const json_value *game = nullptr);

/// Loads and applies saved settings. Call it after actions and axes are registered and the
/// language is loaded, usually in `phase_startup`, so the keys the player changed replace the
/// default keys. If there is no file yet (first run) it does nothing.
/// @param ctx Engine context.
/// @param file File name.
/// @param game Receives the game's own saved data, or null.
/// @return `true` if the file was read.
bool settings_load(context &ctx, const char *file = "settings.json", json_value *game = nullptr);
/// @}
} // namespace njin
