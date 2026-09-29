#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"
#include "njin_dialog.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace njin {
// Core module. In phase_pre_update, while a dialogue is open, reads the
// advance and choice keys (consuming them from the game), runs the
// typewriter and walks the script. The box itself is drawn by dialog_draw,
// after phase_post_render, so it sits over the game's own HUD.
mod_desc dialog_module();

struct dialog_portrait_rec {
  texture_handle texture{};
  rect source{};
};

struct dialog_state {
  dialog_style style = dialog_default_style();
  std::unordered_map<std::string, dialog_portrait_rec> portraits;
  std::function<bool(context &, const std::string &)> condition;

  dialog_script script;
  bool active = false;
  i32 node = -1;
  std::string speaker; // resolved through tr() when it starts with '@'
  std::string text;
  std::vector<i32> choices;          // indexes of the choices shown
  std::vector<std::string> choice_text;
  i32 selected = 0;
  f32 shown = 0.0f; // codepoints revealed
  i32 total = 0;    // codepoints in `text`
  i32 blips = 0;    // blip sounds played for this line
  std::vector<std::string> lines; // `text` wrapped at `wrap_width`
  f32 wrap_width = -1.0f;
  std::vector<rect> choice_rects; // where the choices were drawn, for the mouse
  bool paused_by_us = false;
  std::string last_node;
};

// Draws the open dialogue box, if any. Called by the main loop after
// phase_post_render and before toasts.
void dialog_draw(context &ctx);
} // namespace njin
