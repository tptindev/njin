#pragma once

#include "_math.h"
#include "_types.h"
#include <raylib.h>
#include <string>
#include <vector>

namespace njin {
struct njin_ctx;

// Virtual resolution. While `size` is set, the whole frame (world, UI, toasts,
// fades) is drawn into `target` at that size, then scaled onto the window with
// bars around it. Everything that asks for the screen size gets `size`, and
// the mouse is mapped into it, so games never see the window's real pixels.
// A line of text waiting for the window-resolution pass (see view_state).
struct queued_text {
  std::string text;
  vec2 pos;   // virtual pixels
  f32 size;   // virtual pixels
  rgba color;
  font_handle font;
};

struct view_state {
  vec2 size{};              // 0: off, the frame goes straight to the window
  bool integer_scale = true;
  rgba bars{0.0f, 0.0f, 0.0f, 1.0f};
  RenderTexture2D target{};
  bool drawing = false;     // this frame is being drawn into `target`
  f32 scale = 1.0f;         // window pixels per virtual pixel
  vec2 offset{};            // top-left of the image in the window

  // Text at window resolution. The virtual image is scaled up with a point
  // filter, so text drawn into it is as soft as the scaling is coarse. With a
  // real GPU, screen-space text is queued instead of drawn, and drawn after
  // the scaled image, into the window itself, from an atlas baked at
  // size * scale: sharp at any window size. Text in world space (under the
  // world camera) and text drawn into a render texture stay in the virtual
  // image, where they belong. Queued text sits above everything drawn in the
  // virtual image; a full-screen rectangle drawn later (fade, flash, the
  // dimming behind a modal) tints it, see view_text_cover().
  bool crisp_text = false;             // wanted and possible, set every frame
  mutable i32 world_depth = 0;         // inside begin/end of the world camera
  mutable i32 offscreen_depth = 0;     // inside a render texture
  mutable std::vector<queued_text> text_layer;

  view_state() = default;
  ~view_state();
  view_state(const view_state &) = delete;
  view_state &operator=(const view_state &) = delete;
};

inline bool view_active(const view_state &view) { return view.size.x >= 1.0f && view.size.y >= 1.0f; }

// True when a screen-space draw_text right now goes to the window-resolution
// pass instead of the virtual image.
inline bool view_text_deferred(const view_state &view) {
  return view.crisp_text && view.drawing && view.scale != 1.0f && view.world_depth == 0 &&
         view.offscreen_depth == 0;
}
// A rectangle of `color` was just drawn over the virtual image: when it covers
// all of it, the text already queued is under it, so it takes the tint.
void view_text_cover(const view_state &view, rect r, rgba color);

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
