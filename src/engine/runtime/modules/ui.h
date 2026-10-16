#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"
#include "njin_ui.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace njin {
// Core module. In phase_pre_update, reads this frame's navigation input
// (keys, mouse, gamepads, with key repeat), moves the focus over the widgets
// drawn last frame, and, while a panel was on screen, consumes the keys and
// clicks the UI used so the game underneath does not also see them. The
// widgets themselves are immediate-mode calls made by the game in
// phase_post_render.
mod_desc ui_module();

// A widget as placed this frame, for navigation next frame.
struct ui_widget_rec {
  u64 id = 0;
  rect area{};
  bool adjustable = false; // takes left/right itself (slider, choice)
  u64 panel = 0;           // panel it was drawn in, for modal filtering
};

// One toast waiting on screen. Ages in real time.
struct ui_toast_rec {
  std::string text;
  ui_toast_kind kind = ui_toast_info;
  f32 age = 0.0f;
  f32 life = 2.5f;
};

// One deferred draw inside a panel. Panels draw at ui_end, background first,
// so the background can be sized to what the panel turned out to contain.
struct ui_cmd {
  enum kind_t { skin, text, image, ring, clip_begin, clip_end } kind = skin; // clip_*: `area` a scroll area
  rect area{};
  ui_skin look{};
  shader_handle shader{};
  f32 state = 0.0f;
  f32 value = 0.0f;
  std::string str;
  f32 size = 0.0f;
  rgba color{};
  // text: the font it was measured with (the style's when it was pushed);
  // not set, the style's when it is drawn.
  font_handle font{};
  bool font_set = false;
  texture_handle texture{};
  rect source{};
  vec2 scale{1.0f, 1.0f}; // image: target size over source size
  // ring: `area` is the square it fits in, `color` the fill, `value` 0..1.
  rgba track{};           // ring: the background circle
  f32 thickness = 0.0f;   // ring: width of the band, pixels
  f32 start = 0.0f;       // ring: degrees clockwise from 12 o'clock
  bool clockwise = true;
  bool round_caps = false;
  bool show_track = true;
};

struct ui_state {
  ui_style style = ui_default_style();

  // Input read at the start of the frame, before the UI consumes it.
  bool up = false, down = false, left = false, right = false;
  bool accept = false, accept_held = false, back = false;
  bool back_reported = false;
  bool mouse_pressed = false, mouse_held = false, mouse_released = false, mouse_moved = false;
  bool mouse_over = false; // the mouse is over one of last frame's panels
  u64 mouse_top = 0;       // the one of them on top under the mouse (drawn last), 0 none
  vec2 mouse{};
  i32 adjust = 0; // -1/+1 for the focused slider or choice
  f32 repeat_timer = 0.0f;
  i32 repeat_dir = 0; // 1 up, 2 down, 3 left, 4 right

  u64 focus = 0;
  u64 pressed = 0; // widget the mouse went down on
  std::vector<ui_widget_rec> last, current;
  std::vector<rect> last_panels, panels;
  std::vector<u64> last_panel_ids, panel_ids; // each panel's id, as `panels`

  // Panel being built.
  bool in_panel = false;
  u64 panel_id = 0;
  rect panel{};
  f32 cursor = 0.0f; // y of the next widget
  rect last_rect{};  // slot of the widget placed last (ui_last_rect)
  i32 row_cols = 0, row_index = 0;
  f32 row_y = 0.0f;
  f32 row_h = 0.0f; // the tallest widget of the row so far: the row is as tall as it
  std::vector<ui_cmd> cmds;
  const char *panel_title = nullptr;
  bool panel_background = true;
  bool panel_navigable = true;
  // ui_panel_desc::enter_seconds: the panels drawn this frame and last, the
  // seconds each entering one has been on screen, how opaque the one being
  // built is (its commands are faded by it at ui_end).
  std::vector<u64> shown, last_shown;
  std::unordered_map<u64, f32> enter_age;
  f32 panel_alpha = 1.0f;
  bool panel_inert = false; // ui_panel_desc::leaving or inert: takes no input, not over
  bool panel_covered = false; // a panel drawn after it last frame is over the mouse: this one does not take it
  rect panel_done{};        // the panel ui_end() closed last (ui_panel_rect)

  // ui_style::hover_seconds: how far each button is into its hover face, 0..1;
  // a button leaves the map when it is back at 0.
  std::unordered_map<u64, f32> hover_amount;
  // ui_tabs(): where each bar's open face is, as a tab index that slides to
  // the open one (ui_style::tab_slide_seconds); the icon boxes of the bar
  // drawn last (ui_tabs_icon_rect).
  std::unordered_map<u64, f32> tab_slide;
  std::vector<rect> tab_icons;
  // Whether a panel that takes the navigation keys was drawn this frame / last
  // frame. HUD panels (ui_panel_desc::navigable false) only take the mouse.
  bool navigable_now = false, navigable_last = false;
  std::unordered_map<u64, f32> heights; // measured panel heights, by id
  // A panel taller than the screen is shrunk to fit it: `natural` is the height
  // a panel would have at fit 1 (last frame's height over the fit it was built
  // with), and `fit` multiplies every size of the panel being built (see sc()).
  std::unordered_map<u64, f32> natural;
  f32 fit = 1.0f;

  // Modal popup. `modal` is the popup drawn this frame; `modal_last` the one
  // drawn last frame, which is what input is filtered against (the widget
  // list is a frame old too). Focus is saved when a popup opens and restored
  // when it has gone.
  u64 modal = 0;
  u64 modal_last = 0;
  u64 saved_focus = 0;

  std::vector<ui_toast_rec> toasts;

  // Scroll areas (ui_scroll_begin): the one open, each one's offset and the
  // height of what it held last frame, by id; their boxes this frame and last.
  bool in_scroll = false;
  u64 scroll_id = 0;
  rect scroll_area{};
  f32 scroll_from = 0.0f; // the y its content starts at, before the offset
  std::unordered_map<u64, f32> scroll_offset, scroll_content;
  std::unordered_set<u64> scroll_to_end; // ui_scroll_to_end(): taken to the end when next laid out
  std::vector<rect> scrolls, last_scrolls;
  std::vector<u64> scroll_panel, last_scroll_panel; // the panel each scroll area is in
  // The scroll bar (ui_style::scrollbar_width): what the content leaves free
  // on the right of the area open; the area whose thumb is held, and where on
  // the thumb it was taken (from its top); the open area's thumb hovered.
  f32 scroll_reserve = 0.0f;
  u64 scroll_drag = 0;
  f32 scroll_grab = 0.0f;
  bool scroll_hover = false;
  // ui_let_through(): where the mouse's left button is left to the game, panels or not.
  std::vector<rect> through, last_through;
  std::vector<u64> through_panel, last_through_panel; // the panel each was let through in, 0 outside one
  f32 wheel = 0.0f; // the wheel this frame, if over one of last frame's scroll areas

  std::string tooltip; // ui_tooltip(): drawn by the mouse after the panels, then cleared

  // ui_keybind waiting for the next press: the widget's id, and the device
  // it wants (input_source kind). Navigation is off meanwhile.
  u64 listening = 0;
  i32 listen_kind = 0;

  // ui_input being typed in: its id; whether it was drawn this frame (one
  // not drawn stops taking the keys). What was typed this frame, read at its
  // start before the keys are taken from the game: the text, the backspaces,
  // Enter, Esc.
  u64 editing = 0;
  bool edit_drawn = false;
  std::string edit_typed;
  i32 edit_back = 0;
  bool edit_enter = false, edit_escape = false;
};

// Draws and ages the toasts. Called by the main loop after post_render, so
// they land over the game's UI.
void ui_draw_toasts(context &ctx);

// Draws one face of `look` (0 normal, 1 focused, 2 pressed, 3 disabled) over
// `area`, with its texture, 9-slice and shader. For other engine overlays
// (the dialogue box) that dress like the UI.
void ui_draw_look(context &ctx, const ui_look &look, i32 state, rect area, f32 value = 0.0f);
} // namespace njin
