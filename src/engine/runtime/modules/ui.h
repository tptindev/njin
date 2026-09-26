#pragma once
#include "_mod.h"
#include "njin_ui.h"
#include <string>
#include <unordered_map>
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
  enum kind_t { skin, text, image } kind = skin;
  rect area{};
  ui_skin look{};
  shader_handle shader{};
  f32 state = 0.0f;
  f32 value = 0.0f;
  std::string str;
  f32 size = 0.0f;
  rgba color{};
  texture_handle texture{};
  rect source{};
  vec2 scale{1.0f, 1.0f}; // image: target size over source size
};

struct ui_state {
  ui_style style = ui_default_style();

  // Input read at the start of the frame, before the UI consumes it.
  bool up = false, down = false, left = false, right = false;
  bool accept = false, accept_held = false, back = false;
  bool back_reported = false;
  bool mouse_pressed = false, mouse_held = false, mouse_released = false, mouse_moved = false;
  vec2 mouse{};
  i32 adjust = 0; // -1/+1 for the focused slider or choice
  f32 repeat_timer = 0.0f;
  i32 repeat_dir = 0; // 1 up, 2 down, 3 left, 4 right

  u64 focus = 0;
  u64 pressed = 0; // widget the mouse went down on
  std::vector<ui_widget_rec> last, current;
  std::vector<rect> last_panels, panels;

  // Panel being built.
  bool in_panel = false;
  u64 panel_id = 0;
  rect panel{};
  f32 cursor = 0.0f; // y of the next widget
  i32 row_cols = 0, row_index = 0;
  f32 row_y = 0.0f;
  std::vector<ui_cmd> cmds;
  const char *panel_title = nullptr;
  bool panel_background = true;
  std::unordered_map<u64, f32> heights; // measured panel heights, by id

  // Modal popup. `modal` is the popup drawn this frame; `modal_last` the one
  // drawn last frame, which is what input is filtered against (the widget
  // list is a frame old too). Focus is saved when a popup opens and restored
  // when it has gone.
  u64 modal = 0;
  u64 modal_last = 0;
  u64 saved_focus = 0;

  std::vector<ui_toast_rec> toasts;

  // ui_keybind waiting for the next press: the widget's id, and the device
  // it wants (input_source kind). Navigation is off meanwhile.
  u64 listening = 0;
  i32 listen_kind = 0;
};

// Draws and ages the toasts. Called by the main loop after post_render, so
// they land over the game's UI.
void ui_draw_toasts(njin_ctx &ctx);

// Draws one face of `look` (0 normal, 1 focused, 2 pressed, 3 disabled) over
// `area`, with its texture, 9-slice and shader. For other engine overlays
// (the dialogue box) that dress like the UI.
void ui_draw_look(njin_ctx &ctx, const ui_look &look, i32 state, rect area, f32 value = 0.0f);
} // namespace njin
