#pragma once
#include "_math.h"
#include "_types.h"
#include <initializer_list>
#include <span>
#include <string>

namespace njin {
struct context;

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
void ui_style_set(context &ctx, const ui_style &style);

/// Current style. @param ctx Engine context. @return Style.
ui_style ui_style_get(const context &ctx);

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
  /// The panel takes the navigation keys and the gamepad (arrows, Enter, Space,
  /// Esc). `false` for a HUD that stays up during play: its buttons are clicked
  /// with the mouse only, are never preselected, and those keys stay the game's
  /// (see ui_active()).
  bool navigable = true;
  /// When set, a close box is drawn in the panel's top right corner; clicking
  /// it (mouse only) sets `*open` to `false`. The panel is still drawn that
  /// frame; the game stops calling it from the next. Null means no close box.
  bool *open = nullptr;
};

/// Begins a panel. Widgets called afterwards are stacked top to bottom inside it,
/// until ui_end(). The height is computed from the content. A panel taller than the screen
/// shrinks itself (text included, down to half at most) to fit, instead of overflowing top and bottom.
///
/// Call it in `phase_post_render` (screen space). Only panels
/// called during the frame are shown; to hide a menu, do not call it.
/// @code
/// void menu(njin::context &ctx) {
///   njin::ui_begin(ctx, {.id = "main", .title = "Game name"});
///   if (njin::ui_button(ctx, "Play"))
///     njin::scene_fade(ctx, g.play);
///   if (njin::ui_button(ctx, "Settings"))
///     g.settings_open = true;
///   if (njin::ui_button(ctx, "Quit"))
///     njin::quit(ctx);
///   njin::ui_end(ctx);
/// }
/// @endcode
/// @param ctx Engine context.
/// @param desc Position and size.
void ui_begin(context &ctx, const ui_panel_desc &desc = {});

/// Ends the panel and draws it. @param ctx Engine context.
void ui_end(context &ctx);

/// Lays out the next `columns` widgets as a horizontal row, sharing the width equally.
/// Left/right arrows move between them.
/// @param ctx Engine context.
/// @param columns Number of widgets in the row.
void ui_row(context &ctx, i32 columns);

/// A line of text. In a row (ui_row()) it stands as tall as a widget, its text
/// in the middle top to bottom, level with a button beside it.
/// @param ctx Engine context.
/// @param text Text (UTF-8).
void ui_label(context &ctx, const char *text);

/// Empty space. @param ctx Engine context. @param height Height, pixels (before `scale`).
void ui_space(context &ctx, f32 height);

/// A button.
///
/// Labels must be different within the same panel; for two buttons with the same text add a hidden suffix
/// after `##`: `"Delete##slot1"`, `"Delete##slot2"` (the part after `##` is not drawn).
/// @param ctx Engine context.
/// @param label Label (UTF-8).
/// @param enabled `false` makes the button gray and not pressable.
/// @param selected `true` keeps the button's "pressed" face (ui_look::pressed)
/// while nothing else shows: an open tab, a tool that is on, the chosen speed.
/// @return `true` on the frame the button is pressed (mouse, Enter, Space, A button).
bool ui_button(context &ctx, const char *label, bool enabled = true, bool selected = false);

/// An on/off switch. Press to change.
/// @param ctx Engine context.
/// @param label Label.
/// @param value Value, modified when pressed.
/// @return `true` on the frame the value changes.
bool ui_toggle(context &ctx, const char *label, bool &value);

/// A slider. Drag with the mouse, or use left/right when focused.
/// @param ctx Engine context.
/// @param label Label.
/// @param value Value, modified when dragged.
/// @param min Minimum value.
/// @param max Maximum value.
/// @param step Step when using keys or gamepad, and rounding when dragging. 0 means 1/20 of the range.
/// @param percent Show the value as a percentage of the range instead of a number.
/// @return `true` on the frame the value changes.
bool ui_slider(context &ctx, const char *label, f32 &value, f32 min, f32 max,
               f32 step = 0.0f, bool percent = false);

/// Picks one of several options with left/right or by pressing: difficulty, resolution,
/// language.
/// @param ctx Engine context.
/// @param label Label.
/// @param index Current choice, modified when changed. Wraps around at both ends.
/// @param options The options.
/// @return `true` on the frame the choice changes.
bool ui_choice(context &ctx, const char *label, i32 &index,
               std::initializer_list<const char *> options);

/// Picks one of several options (dynamic list).
/// @param ctx Engine context.
/// @param label Label.
/// @param index Current choice.
/// @param options The options as strings.
/// @return `true` on the frame the choice changes.
bool ui_choice(context &ctx, const char *label, i32 &index,
               std::span<const std::string> options);

/// A progress bar, not pressable: health, reload time.
/// @param ctx Engine context.
/// @param value Progress 0..1.
/// @param text Text drawn in the middle of the bar. May be null.
/// @param fill Colour of the filled part, for a bar with its own meaning (health
/// red when low). Alpha 0: ui_style::fill.
void ui_progress(context &ctx, f32 value, const char *text = nullptr, rgba fill = {0.0f, 0.0f, 0.0f, 0.0f});

/// How ui_text() draws a line of text.
struct ui_text_desc {
  rgba color{0.0f, 0.0f, 0.0f, 0.0f}; ///< Colour. Alpha 0: the colour of ui_label() (ui_style::label).
  f32 size = 0.0f;  ///< Font size, pixels (before `scale`). 0 means ui_style::font_size.
  f32 align = 0.0f; ///< Horizontal alignment in its slot: 0 left, 0.5 centre, 1 right.
};

/// A line of text like ui_label(), with its own colour, size and alignment: an
/// amount of money in green or red, a value aligned right beside its name in a row.
/// @code
/// njin::ui_row(ctx, 2);
/// njin::ui_label(ctx, "Money");
/// njin::ui_text(ctx, "1500k", {.color = {0.5f, 0.8f, 0.4f, 1.0f}, .align = 1.0f});
/// @endcode
/// @param ctx Engine context.
/// @param text Text (UTF-8).
/// @param desc Colour, size, alignment.
void ui_text(context &ctx, const char *text, const ui_text_desc &desc = {});

/// A small note beside the mouse pointer, drawn over every panel at the end of
/// the frame. Call it every frame it should show (usually while the mouse is
/// over the thing it explains); once no longer called, it is gone. Callable
/// from anywhere, no ui_begin needed.
/// @code
/// const bool pause = njin::ui_button(ctx, "||");
/// if (njin::point_in_rect(njin::mouse_pos(ctx), njin::ui_last_rect(ctx)))
///   njin::ui_tooltip(ctx, "Pause (Space)");
/// @endcode
/// @param ctx Engine context.
/// @param text Text (UTF-8).
void ui_tooltip(context &ctx, const char *text);

/// Starts a scroll area `height` tall in the open panel: the widgets called
/// after it, up to ui_scroll_end(), are laid out inside it and only the part
/// within it shows. The mouse wheel over the area scrolls it (the game does not
/// see that wheel); a widget focused with the keys outside the view scrolls
/// the area to it.
/// @code
/// njin::ui_scroll_begin(ctx, "list", 240.0f);
/// for (const shop &s : shops)
///   if (njin::ui_button(ctx, s.name.c_str()))
///     open_shop(s);
/// njin::ui_scroll_end(ctx);
/// @endcode
/// @param ctx Engine context.
/// @param id Name of the area, unique in the panel: it remembers how far it was scrolled.
/// @param height Height, pixels (before `scale`).
void ui_scroll_begin(context &ctx, const char *id, f32 height);

/// Ends the scroll area started with ui_scroll_begin(). @param ctx Engine context.
void ui_scroll_end(context &ctx);

/// Describes a circular progress bar, for ui_progress_circle().
/// Every size is in pixels before `ui_style::scale`. A colour with alpha 0 comes from ui_style.
struct ui_circle_desc {
  f32 value = 0.0f;       ///< Progress 0..1.
  f32 diameter = 96.0f;   ///< Outer diameter.
  f32 thickness = 10.0f;  ///< Width of the ring. Half the diameter or more makes a solid disc.
  /// Where it starts, in degrees from the top (12 o'clock), clockwise: 90 is 3 o'clock, -90 is 9 o'clock.
  f32 start_angle = 0.0f;
  bool clockwise = true;   ///< `false`: fills counter-clockwise.
  bool round_caps = false; ///< Rounds both ends of the filled part.
  bool show_track = true;  ///< Draw the background ring under the filled part.
  rgba track{0.0f, 0.0f, 0.0f, 0.0f}; ///< Colour of the background ring. Alpha 0: uses ui_style::track.
  rgba fill{0.0f, 0.0f, 0.0f, 0.0f};  ///< Colour of the filled part. Alpha 0: uses ui_style::fill.
  const char *text = nullptr;         ///< Text in the middle. When null, see `percent`.
  bool percent = false;               ///< When `text` is null: draws the progress as "75%" in the middle.
  rgba text_color{0.0f, 0.0f, 0.0f, 0.0f}; ///< Text colour. Alpha 0: uses ui_style::panel.text.
};

/// A circular progress bar, not pressable: cooldowns, reloads, a ring-shaped health bar.
/// Takes one line as tall as the diameter, centred in the panel (or in the column of a ui_row()).
/// @code
/// njin::ui_progress_circle(ctx, {.value = cooldown, .diameter = 64, .thickness = 8,
///                                .round_caps = true, .fill = {0.9f, 0.3f, 0.2f, 1.0f}});
/// @endcode
/// @param ctx Engine context.
/// @param desc The value and the look.
void ui_progress_circle(context &ctx, const ui_circle_desc &desc);

/// An image, centered in the panel.
/// @param ctx Engine context.
/// @param texture Image.
/// @param size Drawn size, pixels (before `scale`).
/// @param source Region in the image. Size 0 means the whole image.
void ui_image(context &ctx, texture_handle texture, vec2 size, rect source = {});

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
bool ui_keybind(context &ctx, const char *label, action_handle action, bool pad = false);

/// Whether any ui_keybind() row is waiting for a key. While that is the case, do not treat Esc as
/// "close menu".
/// @param ctx Engine context.
/// @return `true` if waiting.
bool ui_keybind_listening(const context &ctx);

/// How a text field looks and what it holds, used with ui_input().
struct ui_input_desc {
  /// Faded text shown while the field is empty and not being typed in. Null: the part of the label before `##`.
  const char *hint = nullptr;
  /// Most characters (not bytes) it holds. 0 is no limit.
  i32 max_chars = 64;
};

/// A one-line text field: a search box, a character's name, a save file's name.
///
/// Clicking the field (or Enter while it is selected) starts typing. While
/// typing, the keyboard belongs to this field: the game sees no key pressed or
/// held (WASD and Esc included), and ui_typing() returns `true`. Characters
/// come from text_char(), so they have been through the operating system's
/// keyboard layout and input method, and Vietnamese with diacritics can be
/// typed. Backspace (repeating while held) deletes the last character. Enter,
/// Esc or a mouse click outside the field stops typing and keeps the text. A
/// field that is no longer drawn (its menu closed) stops too. Text longer than
/// the field shows only its end.
/// @code
/// static std::string search;
/// njin::ui_begin(ctx, {.id = "shop"});
/// if (njin::ui_input(ctx, "Tìm##search", search, {.hint = "Tên món đồ"}))
///   filter(search);
/// njin::ui_end(ctx);
/// @endcode
/// @param ctx The engine context.
/// @param label Label (UTF-8), the field's identifier; the part before `##` is
/// the default hint.
/// @param text The field's text, UTF-8; the field edits it in place.
/// @param desc Hint and limit.
/// @return `true` in the frame the text changes.
bool ui_input(context &ctx, const char *label, std::string &text, const ui_input_desc &desc = {});

/// Whether some ui_input() is being typed in. Meanwhile the game gets no keys;
/// use this so as not to take keys read some other way as shortcuts.
/// @param ctx The engine context.
/// @return `true` while typing.
bool ui_typing(const context &ctx);

/// `true` on the frame the player presses back (Esc, Backspace, B button) while
/// a panel is shown. Use it to close a submenu or return to the previous screen.
/// @param ctx Engine context.
/// @return `true` if back was just pressed.
bool ui_back(context &ctx);

/// Preselects the widget with label `label` (in the currently open panel), usually called right after
/// opening a menu so the default button is selected for the gamepad.
/// @param ctx Engine context.
/// @param label Full label, including the `##` part.
void ui_focus(context &ctx, const char *label);

/// Whether a panel that takes the keys was drawn in the previous frame (HUD
/// panels with `navigable = false` do not count). While that is the case the UI
/// takes the navigation keys (arrows, Enter, Space, Esc) and the game does not
/// see them, so the character does not move while the player is choosing from a menu.
/// @param ctx Engine context.
/// @return `true` if the UI is shown.
bool ui_active(const context &ctx);

/// Frame of the widget just placed in the open panel (ui_label(), ui_button(),
/// ui_space()...), in screen pixels. Use it to draw more on a widget: a picture
/// in a button, a minimap in a ui_space() gap. The panel is only drawn in
/// ui_end(), so draw the extra after ui_end(), or the panel background covers it.
/// @code
/// njin::ui_begin(ctx, {.id = "shop"});
/// const bool buy = njin::ui_button(ctx, "##sword");
/// const njin::rect icon = njin::ui_last_rect(ctx);
/// njin::ui_end(ctx);
/// njin::texture_draw(ctx, g.sword, icon.pos);
/// @endcode
/// @param ctx Engine context.
/// @return Frame of the widget placed last.
rect ui_last_rect(const context &ctx);

/// Whether the mouse is over a panel (last frame's panels). There the UI has
/// already taken the left mouse button; this lets the game skip the right
/// button, the wheel or its own cursor too, for example not ordering units
/// when the right button is clicked on the HUD.
/// @param ctx Engine context.
/// @return `true` if the mouse is over a panel.
bool ui_mouse_over(const context &ctx);

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
void ui_toast(context &ctx, const char *text, const ui_toast_desc &desc = {});

/// Clears every toast being shown, for example when changing scene. @param ctx Engine context.
void ui_toast_clear(context &ctx);

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
///   if (pick == 1) njin::quit(ctx);
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
i32 ui_popup(context &ctx, const ui_popup_desc &desc, bool &open);

/// Begins a popup with arbitrary content: darkens the background, opens a modal panel. Call normal
/// widgets (ui_button, ui_slider...), then ui_popup_end(). Close the popup
/// by no longer calling it; use njin::ui_back() to catch the back key.
///
/// Only `id`, `title` and `width` of `desc` are used.
/// @param ctx Engine context.
/// @param desc Popup description.
void ui_popup_begin(context &ctx, const ui_popup_desc &desc);

/// Ends a popup begun with ui_popup_begin(). @param ctx Engine context.
void ui_popup_end(context &ctx);
/// @}
} // namespace njin
