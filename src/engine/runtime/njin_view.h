#pragma once

#include "_types.h"
#include <raylib.h>

namespace njin {
struct njin_ctx;

// Virtual resolution. While `size` is set, the whole frame (world, UI, toasts,
// fades) is drawn into `target` at that size, then scaled onto the window with
// bars around it. Everything that asks for the screen size gets `size`, and
// the mouse is mapped into it, so games never see the window's real pixels.
struct view_state {
  vec2 size{};              // 0: off, the frame goes straight to the window
  bool integer_scale = true;
  rgba bars{0.0f, 0.0f, 0.0f, 1.0f};
  RenderTexture2D target{};
  bool drawing = false;     // this frame is being drawn into `target`
  f32 scale = 1.0f;         // window pixels per virtual pixel
  vec2 offset{};            // top-left of the image in the window

  view_state() = default;
  ~view_state();
  view_state(const view_state &) = delete;
  view_state &operator=(const view_state &) = delete;
};

inline bool view_active(const view_state &view) { return view.size.x >= 1.0f && view.size.y >= 1.0f; }

// Works out scale and offset for this frame's window size. Called at the top
// of the frame, before input is read.
void view_frame_begin(view_state &view);
// Maps the mouse position and motion just polled into virtual pixels.
void view_map_mouse(const view_state &view, vec2 &pos, vec2 &motion);
// Right after BeginDrawing: starts drawing into the virtual target.
void view_draw_begin(view_state &view, Color clear);
// Rebinds the virtual target after something ended a texture mode of its own
// mid-frame (EndTextureMode always returns to the window).
void view_rebind(const view_state &view);
// Before EndDrawing: puts the virtual image on the window.
void view_draw_end(view_state &view);
} // namespace njin
