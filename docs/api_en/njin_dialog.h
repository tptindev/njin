#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_json.h"
#include "njin_ui.h"
#include <functional>
#include <string>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_dialog
/// @{

/// One reply choice in a dialog.
struct dialog_choice {
  std::string text;  ///< Displayed text. Starting with `@` means a translation key (see tr()).
  std::string next;  ///< Node to go to when chosen. Empty ends the conversation.
  std::string event; ///< Name of the event sent when chosen (njin::dialog_event). May be empty.
  std::string cond;  ///< Shown only when this condition is true (see dialog_set_condition()).
};

/// One line of dialog: who speaks, what they say, then where to go.
struct dialog_node {
  std::string id;       ///< Node name, for `next` to point to.
  std::string speaker;  ///< Speaker name. Empty means narration. `@` means a translation key.
  std::string text;     ///< Content. `@` means a translation key. Wraps automatically.
  std::string portrait; ///< Name of a portrait registered with dialog_portrait(). May be empty.
  std::string next;     ///< Next node when there are no choices. Empty means the end.
  std::string event;    ///< Event sent when this line appears. May be empty.
  std::string cond;     ///< Skip this line (go straight to `next`) when the condition is false.
  std::vector<dialog_choice> choices; ///< The choices, shown after the text finishes running.
};

/// A conversation: several nodes linked together.
///
/// Usually written in JSON and loaded with dialog_load():
/// @code{.json}
/// { "start": "hi", "nodes": [
///   { "id": "hi", "speaker": "Old man", "portrait": "oldman",
///     "text": "It is dangerous to go alone. Take this.", "next": "ask" },
///   { "id": "ask", "speaker": "Old man", "text": "Would you like to hear a story?",
///     "choices": [ { "text": "Yes, please", "next": "story" },
///                  { "text": "Maybe later", "event": "refused" } ] },
///   { "id": "story", "text": "...", "event": "got_sword" } ] }
/// @endcode
/// A node without an `id` defaults to going to the node right after it in the list.
struct dialog_script {
  std::vector<dialog_node> nodes; ///< The nodes.
  std::string start;              ///< Start node. Empty means the first node.
};

/// Reads a conversation from JSON (the form is in njin::dialog_script).
/// @param json Data.
/// @param out Receives the conversation.
/// @return `false` if `nodes` is missing.
bool dialog_parse(const json_value &json, dialog_script &out);

/// Loads a conversation from a JSON file.
/// @param path Path.
/// @param out Receives the conversation.
/// @return `false` if it could not be read (a log is written).
bool dialog_load(const char *path, dialog_script &out);

/// Look of the dialog box. Get the default with dialog_default_style().
///
/// The box, text and choices are drawn with `ui_look` like the UI, so 9-slice
/// images and custom shaders work.
struct dialog_style {
  font_handle font{};       ///< Font. Defaults to the UI font.
  f32 font_size = 26.0f;    ///< Text size, in pixels.
  f32 scale = 1.0f;         ///< Multiplies every size.
  i32 lines = 3;            ///< Number of text lines in the box.
  f32 margin = 24.0f;       ///< Distance from the screen edge.
  f32 padding = 18.0f;      ///< Distance from the box edge to the text.
  f32 max_width = 1100.0f;  ///< Maximum width of the box.
  bool top = false;         ///< Place the box at the top of the screen instead of the bottom.
  f32 chars_per_second = 45.0f; ///< Speed the text runs at. 0 shows the whole line at once.
  ui_look box{};            ///< Box background (`normal`) and text color (`text`).
  ui_look name{};           ///< Speaker name tag: background (`normal`) and text color (`text`).
  ui_look choice{};         ///< Choices: `normal`, `focused` and the matching text colors.
  vec2 portrait_size{0.0f, 0.0f}; ///< Portrait size. 0 fits the text height.
  sound_handle sound_blip{};   ///< Played while the text runs (every few characters). May be left empty.
  sound_handle sound_next{};   ///< Played when advancing to the next line or choosing.
  /// Stops the game's time (time_set_paused()) while the dialog is open.
  bool pause_game = false;
  /// Action also used to advance, besides Enter, Space, the A button and left mouse button.
  action_handle advance{};
};

/// Default look: a dark box at the bottom, matching the default UI.
/// @return The default style.
dialog_style dialog_default_style();

/// Sets the look of the dialog box. @param ctx Engine context. @param style Style.
void dialog_set_style(njin_ctx &ctx, const dialog_style &style);

/// Current look. @param ctx Engine context. @return Style.
dialog_style dialog_get_style(const njin_ctx &ctx);

/// Registers a portrait for lines to refer to by name (`"portrait": "oldman"`).
/// @param ctx Engine context.
/// @param name Name.
/// @param texture Image.
/// @param source Region in the image. A size of 0 means the whole image.
void dialog_portrait(njin_ctx &ctx, const char *name, texture_handle texture, rect source = {});

/// Function that checks the `cond` condition of lines and choices: takes the
/// condition string and returns true or false. The game decides the syntax, for
/// example the name of a flag in the save game (`"has_key"`), with a leading `!` to negate.
/// @param ctx Engine context.
/// @param fn Check function. If left empty, every condition is true.
void dialog_set_condition(njin_ctx &ctx, std::function<bool(njin_ctx &, const std::string &)> fn);

/// Starts a conversation. The engine draws and drives the dialog box until it
/// ends; the game only needs to listen for events.
///
/// While open, the dialog takes Enter, Space, the A button, left mouse button (and the
/// arrow keys when there are choices), so the game does not see those keys. The game's
/// movement keys are not blocked: check dialog_active() or enable dialog_style::pause_game.
/// @param ctx Engine context.
/// @param script The conversation. It is copied, so it may be destroyed right after.
/// @param start Start node, or null for `script.start`.
void dialog_start(njin_ctx &ctx, const dialog_script &script, const char *start = nullptr);

/// Says a single line, no script needed: signs, objects.
/// @param ctx Engine context.
/// @param speaker Speaker, may be null.
/// @param text Content.
/// @param portrait Portrait name, may be null.
void dialog_say(njin_ctx &ctx, const char *speaker, const char *text, const char *portrait = nullptr);

/// Whether the dialog is open. @param ctx Engine context. @return `true` if open.
bool dialog_active(const njin_ctx &ctx);

/// Closes the dialog immediately (sends njin::dialog_ended). @param ctx Engine context.
void dialog_stop(njin_ctx &ctx);

/// Event: a line or choice with an `event` was just triggered.
struct dialog_event {
  std::string name; ///< Event name in the script.
  std::string node; ///< Node that emitted it.
};

/// Event: the conversation just ended.
struct dialog_ended {
  std::string last_node; ///< The last node that was shown.
};
/// @}
} // namespace njin
