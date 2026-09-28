#pragma once
#include "_math.h"
#include "_types.h"
#include <initializer_list>
#include <span>
#include <string>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_ui
/// @{

/// How one face of a widget is drawn: flat color, image (optionally 9-slice), or both.
///
/// Without an image, a `color` rectangle is drawn (rounded by `roundness`). With an image,
/// the image is drawn multiplied by `color`; `border > 0` cuts the image into 9 pieces (9-slice):
/// the four corners stay unchanged, the edges and center are stretched, so a frame drawn once works
/// for every button size.
struct ui_skin {
  texture_handle texture{};          ///< Image. If invalid, a flat color is drawn.
  rect source{};                     ///< Region in the image, pixels. Size 0 means the whole image.
  f32 border = 0.0f;                 ///< Thickness of the 9-slice border in the image, pixels. 0 stretches the whole image.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Background color (no image) or color multiplied into the image.
  f32 roundness = 0.0f;              ///< Corner rounding when there is no image, 0..1.
  rgba outline{0.0f, 0.0f, 0.0f, 0.0f}; ///< Outline color when there is no image. Alpha 0 means no outline.
  f32 outline_width = 0.0f;          ///< Outline thickness, pixels.
};

/// Look of one kind of widget in each state.
///
/// `shader` (optional) is enabled when drawing the widget's faces. The engine sets the following
/// uniforms itself if the shader declares them (if not, they are skipped without a warning):
///
/// | Uniform | Type | Value |
/// |---|---|---|
/// | `uiState` | float | 0 normal, 1 focused, 2 pressed, 3 disabled |
/// | `uiTime` | float | Seconds since start, for animated effects |
/// | `uiRect` | vec4 | x, y, width, height of the widget, screen pixels |
/// | `uiValue` | float | Progress 0..1 of a slider or progress bar, otherwise 0 |
struct ui_look {
  ui_skin normal{};   ///< Normal.
  ui_skin focused{};  ///< Focused (mouse hovering, or keyboard / gamepad selected).
  ui_skin pressed{};  ///< Being held down.
  ui_skin disabled{}; ///< Disabled.
  rgba text{1.0f, 1.0f, 1.0f, 1.0f};          ///< Normal text color.
  rgba text_focused{1.0f, 1.0f, 1.0f, 1.0f};  ///< Text color when focused.
  rgba text_disabled{0.5f, 0.5f, 0.5f, 1.0f}; ///< Text color when disabled.
  shader_handle shader{}; ///< Custom shader for the widget's faces. May be left empty.
};

/// The whole look of the UI: font, sizes, look of each kind of widget,
/// sounds. Get the default with ui_default_style(), edit it, then call ui_style_set().
struct ui_style {
  font_handle font{};    ///< Font. Defaults to the engine's font.
  f32 font_size = 26.0f; ///< Text size, pixels.
  f32 scale = 1.0f;      ///< Multiplies every size. Set it from the screen height so the UI scales.
  f32 padding = 18.0f;   ///< Distance from the panel edge to the content.
  f32 spacing = 10.0f;   ///< Distance between two widgets.
  f32 widget_height = 46.0f; ///< Height of one widget row.
  f32 width = 380.0f;    ///< Default panel width.

  ui_look panel{};  ///< Panel background (only `normal` is used) and title text color (`text`).
  ui_look label{};  ///< Plain text (only the text color is used).
  ui_look button{}; ///< Button; also the row background of toggle, slider, choice.
  ui_look track{};  ///< Slider track, progress bar, toggle box.
  ui_look fill{};   ///< Filled part of a slider, progress bar, toggle check mark.
  ui_look knob{};   ///< Slider drag knob.
  ui_look toast{};  ///< Toast background and text color (only `normal` and `text` are used).

  /// Overlay color that darkens the background behind a popup.
  rgba dim{0.0f, 0.0f, 0.0f, 0.6f};
  /// Colored stripe on the left of a toast by kind: info, success, warning, error.
  rgba toast_accent[4] = {{0.26f, 0.56f, 0.98f, 1.0f}, {0.25f, 0.75f, 0.42f, 1.0f},
                          {0.96f, 0.72f, 0.20f, 1.0f}, {0.92f, 0.32f, 0.34f, 1.0f}};
  /// Screen corner the toasts anchor to, as a ratio: `{1, 1}` is the bottom right corner, `{0.5, 0}`
  /// is the middle of the top edge. Toasts stack inward from that corner.
  vec2 toast_anchor{1.0f, 1.0f};
  vec2 toast_margin{24.0f, 24.0f}; ///< Distance from the screen edge, pixels (before `scale`).
  f32 toast_seconds = 2.5f;        ///< Default display time.
  i32 toast_max = 5;               ///< Maximum number of toasts at once. The oldest is dropped.
  f32 toast_width = 420.0f;        ///< Maximum width, pixels (before `scale`). Long text wraps automatically.

  sound_handle sound_move{};   ///< Played when the selection moves. May be left empty.
  sound_handle sound_accept{}; ///< Played when a button is pressed or a value changes.
  sound_handle sound_back{};   ///< Played when ui_back() returns `true`.
};

/// Default look: transparent dark panel, rounded buttons, blue accent color.
/// @return The default style.
ui_style ui_default_style();

/// Sets the style for every widget drawn afterwards. Can be changed midway (for example a red
/// warning panel).
/// @param ctx Engine context.
/// @param style New style.
void ui_style_set(njin_ctx &ctx, const ui_style &style);

/// Current style. @param ctx Engine context. @return Style.
ui_style ui_style_get(const njin_ctx &ctx);

/// Position and size of a panel, used with ui_begin().
struct ui_panel_desc {
  /// Unique name of the panel. Used to remember size and selection between frames.
  const char *id = "panel";
  /// Title drawn at the top of the panel. May be null.
  const char *title = nullptr;
  /// Anchor point on the screen, as a ratio: `{0.5, 0.5}` is the center, `{0, 1}` is the bottom left corner.
  vec2 anchor{0.5f, 0.5f};
  /// Point of the panel placed at `anchor`, as a ratio of the panel size. Defaults to the center.
  vec2 pivot{0.5f, 0.5f};
  vec2 offset{};          ///< Extra offset, pixels.
  f32 width = 0.0f;       ///< Width, pixels. 0 means `ui_style::width`.
  bool background = true; ///< Draw the panel background.
};

/// Begins a panel. Widgets called afterwards are stacked top to bottom inside it,
/// until ui_end(). The height is computed from the content. A panel taller than the screen
/// shrinks itself (text included, down to half at most) to fit, instead of overflowing top and bottom.
///
/// Call it in `phase_post_render` (screen space). Only panels
/// called during the frame are shown; to hide a menu, do not call it.
/// @code
/// void menu(njin::njin_ctx &ctx) {
///   njin::ui_begin(ctx, {.id = "main", .title = "Game name"});
///   if (njin::ui_button(ctx, "Play"))
///     njin::scene_fade(ctx, g.play);
///   if (njin::ui_button(ctx, "Settings"))
///     g.settings_open = true;
///   if (njin::ui_button(ctx, "Quit"))
///     njin::njin_quit(ctx);
///   njin::ui_end(ctx);
/// }
/// @endcode
/// @param ctx Engine context.
/// @param desc Position and size.
void ui_begin(njin_ctx &ctx, const ui_panel_desc &desc = {});

/// Ends the panel and draws it. @param ctx Engine context.
void ui_end(njin_ctx &ctx);

/// Lays out the next `columns` widgets as a horizontal row, sharing the width equally.
/// Left/right arrows move between them.
/// @param ctx Engine context.
/// @param columns Number of widgets in the row.
void ui_row(njin_ctx &ctx, i32 columns);

/// A line of text. @param ctx Engine context. @param text Text (UTF-8).
void ui_label(njin_ctx &ctx, const char *text);

/// Empty space. @param ctx Engine context. @param height Height, pixels (before `scale`).
void ui_space(njin_ctx &ctx, f32 height);

/// A button.
///
/// Labels must be different within the same panel; for two buttons with the same text add a hidden suffix
/// after `##`: `"Delete##slot1"`, `"Delete##slot2"` (the part after `##` is not drawn).
/// @param ctx Engine context.
/// @param label Label (UTF-8).
/// @param enabled `false` makes the button gray and not pressable.
/// @return `true` on the frame the button is pressed (mouse, Enter, Space, A button).
bool ui_button(njin_ctx &ctx, const char *label, bool enabled = true);

/// An on/off switch. Press to change.
/// @param ctx Engine context.
/// @param label Label.
/// @param value Value, modified when pressed.
/// @return `true` on the frame the value changes.
bool ui_toggle(njin_ctx &ctx, const char *label, bool &value);

/// A slider. Drag with the mouse, or use left/right when focused.
/// @param ctx Engine context.
/// @param label Label.
/// @param value Value, modified when dragged.
/// @param min Minimum value.
/// @param max Maximum value.
/// @param step Step when using keys or gamepad, and rounding when dragging. 0 means 1/20 of the range.
/// @param percent Show the value as a percentage of the range instead of a number.
/// @return `true` on the frame the value changes.
bool ui_slider(njin_ctx &ctx, const char *label, f32 &value, f32 min, f32 max,
               f32 step = 0.0f, bool percent = false);

/// Picks one of several options with left/right or by pressing: difficulty, resolution,
/// language.
/// @param ctx Engine context.
/// @param label Label.
/// @param index Current choice, modified when changed. Wraps around at both ends.
/// @param options The options.
/// @return `true` on the frame the choice changes.
bool ui_choice(njin_ctx &ctx, const char *label, i32 &index,
               std::initializer_list<const char *> options);

/// Picks one of several options (dynamic list).
/// @param ctx Engine context.
/// @param label Label.
/// @param index Current choice.
/// @param options The options as strings.
/// @return `true` on the frame the choice changes.
bool ui_choice(njin_ctx &ctx, const char *label, i32 &index,
               std::span<const std::string> options);

/// A progress bar, not pressable: health, reload time.
/// @param ctx Engine context.
/// @param value Progress 0..1.
/// @param text Text drawn in the middle of the bar. May be null.
void ui_progress(njin_ctx &ctx, f32 value, const char *text = nullptr);

/// An image, centered in the panel.
/// @param ctx Engine context.
/// @param texture Image.
/// @param size Drawn size, pixels (before `scale`).
/// @param source Region in the image. Size 0 means the whole image.
void ui_image(njin_ctx &ctx, texture_handle texture, vec2 size, rect source = {});

/// A key rebind row for the settings screen: the name on the left, the key
/// currently bound to `action` on the right. Pressing it makes the row wait for a new key ("..."); the next key (or
/// mouse button) pressed is bound in place of the old one with action_rebind(). Esc
/// cancels. While waiting, the UI does not navigate.
/// @code
/// njin::ui_keybind(ctx, "Jump", g.jump);            // keyboard
/// njin::ui_keybind(ctx, "Jump##pad", g.jump, true); // gamepad
/// @endcode
/// Save the new key with settings_save() (or input_bindings_save()).
/// @param ctx Engine context.
/// @param label Label.
/// @param action Action to rebind.
/// @param pad `true` to rebind the gamepad button instead of the key.
/// @return `true` on the frame the key was just changed.
bool ui_keybind(njin_ctx &ctx, const char *label, action_handle action, bool pad = false);

/// Whether any ui_keybind() row is waiting for a key. While that is the case, do not treat Esc as
/// "close menu".
/// @param ctx Engine context.
/// @return `true` if waiting.
bool ui_keybind_listening(const njin_ctx &ctx);

/// `true` on the frame the player presses back (Esc, Backspace, B button) while
/// a panel is shown. Use it to close a submenu or return to the previous screen.
/// @param ctx Engine context.
/// @return `true` if back was just pressed.
bool ui_back(njin_ctx &ctx);

/// Preselects the widget with label `label` (in the currently open panel), usually called right after
/// opening a menu so the default button is selected for the gamepad.
/// @param ctx Engine context.
/// @param label Full label, including the `##` part.
void ui_focus(njin_ctx &ctx, const char *label);

/// Whether any panel was drawn in the previous frame. While that is the case the UI takes the navigation
/// keys (arrows, Enter, Space, Esc) and the game does not see them, so the
/// character does not move while the player is choosing from a menu.
/// @param ctx Engine context.
/// @return `true` if the UI is shown.
bool ui_active(const njin_ctx &ctx);

/// Toast kind: decides the color of the left stripe (see ui_style::toast_accent).
enum ui_toast_kind {
  ui_toast_info,    ///< Neutral information.
  ui_toast_success, ///< Something just done successfully.
  ui_toast_warning, ///< Warning.
  ui_toast_error,   ///< Error.
};

/// How a toast is shown.
struct ui_toast_desc {
  ui_toast_kind kind = ui_toast_info; ///< Kind.
  f32 seconds = 0.0f; ///< Display time. 0 means `ui_style::toast_seconds`.
};

/// Shows a small notification in a screen corner that disappears by itself: "Game saved", "Picked up
/// 5 gold".
///
/// Call it from anywhere, in any phase, **without** ui_begin. The engine queues them,
/// slides them in, fades them out and draws them over everything except the scene transition effect. Long text
/// wraps automatically.
///
/// A toast is read-only and **does not swallow the game's keys or mouse**, unlike the UI
/// panels. Time uses real time: it keeps running while the game is paused or in hitstop.
/// The look comes from ui_style::toast, so images, 9-slice and shaders can be used
/// like on any other widget.
/// @code
/// njin::ui_toast(ctx, "Game saved", {.kind = njin::ui_toast_success});
/// @endcode
/// @param ctx Engine context.
/// @param text Content (UTF-8).
/// @param desc Kind and display time.
void ui_toast(njin_ctx &ctx, const char *text, const ui_toast_desc &desc = {});

/// Clears every toast being shown, for example when changing scene. @param ctx Engine context.
void ui_toast_clear(njin_ctx &ctx);

/// Describes a popup, used with ui_popup() and ui_popup_begin().
struct ui_popup_desc {
  const char *id = "popup";     ///< Unique name of the popup.
  const char *title = nullptr;  ///< Title. May be null.
  const char *message = nullptr; ///< Content, wraps automatically. Only used with ui_popup().
  /// Button labels, at most 4, the rest left null. Only used with ui_popup().
  /// 1 to 3 buttons are laid out in a row, 4 buttons are stacked vertically.
  const char *buttons[4] = {"OK", nullptr, nullptr, nullptr};
  i32 default_button = 0; ///< Button preselected for keyboard and gamepad. Should be the safe button.
  /// Index returned when the player presses back (Esc, Backspace, B). -1 closes
  /// the popup without reporting any button.
  i32 cancel_button = -1;
  f32 width = 0.0f; ///< Width, pixels. 0 means `ui_style::width`.
};

/// A confirmation popup: dark background, title, content and a few buttons.
///
/// Call it every frame in `phase_post_render` while the popup is open; `open` is
/// kept by the game. The popup closes itself (sets `open = false`) when a button is pressed or back is pressed.
/// @code
/// if (want_quit) {
///   const njin::i32 pick = njin::ui_popup(ctx, {.id = "quit", .title = "Quit game?",
///       .message = "Unsaved progress will be lost.", .buttons = {"Stay", "Quit"},
///       .cancel_button = 0}, want_quit);
///   if (pick == 1) njin::njin_quit(ctx);
/// }
/// @endcode
/// The popup is **modal**: other panels are still drawn but receive no mouse, keys
/// or gamepad until the popup closes, and njin::ui_back() only reports to the popup.
/// When opened, the `default_button` button is preselected; when closed, the selection returns to its
/// previous place. On the frame the popup has just opened the back button is ignored, so the same Esc press
/// that opened it does not close it immediately.
/// @param ctx Engine context.
/// @param desc Popup description.
/// @param open Whether the popup is open. Set to `false` when it closes.
/// @return Index of the button just pressed this frame (per `desc.buttons`,
/// `desc.cancel_button` if back was pressed), or -1 if nothing yet.
i32 ui_popup(njin_ctx &ctx, const ui_popup_desc &desc, bool &open);

/// Begins a popup with arbitrary content: darkens the background, opens a modal panel. Call normal
/// widgets (ui_button, ui_slider...), then ui_popup_end(). Close the popup
/// by no longer calling it; use njin::ui_back() to catch the back key.
///
/// Only `id`, `title` and `width` of `desc` are used.
/// @param ctx Engine context.
/// @param desc Popup description.
void ui_popup_begin(njin_ctx &ctx, const ui_popup_desc &desc);

/// Ends a popup begun with ui_popup_begin(). @param ctx Engine context.
void ui_popup_end(njin_ctx &ctx);
/// @}
} // namespace njin
