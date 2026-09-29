#pragma once
#include "njin_internal_only.h"

#include "_math.h"
#include "_types.h"
#include <raylib.h>
#include <string>
#include <vector>

namespace njin {
struct context;

// A line of text waiting for the window-resolution pass (see view_state).
struct queued_text {
  std::string text;
  vec2 pos;         // virtual pixels
  f32 size;         // virtual pixels
  rgba color;
  font_handle font;
  rect bounds;      // where its glyphs can land, virtual pixels
  rect clip;        // clip_begin() area when it was queued; size 0 for none
  usize occluders;  // how many occluders existed then: later ones cover it
};

// Something drawn into the virtual image after some text was queued, which
// therefore sits over that text: a panel, a button, the dimming behind a popup.
struct text_occluder {
  rect area;   // virtual pixels
  rgba color;  // what the text under it blends towards, by color.a
  bool hide;   // an image of unknown colour: the text under it is dropped
};

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

  // Supersampling (config::render_scale). `target` is `render_scale` times
  // bigger than the logical size in each dimension (view_logical_size()) and
  // downscaled with a bilinear filter in view_draw_end(); the projection stays
  // at logical size (see bind_view_target() in njin_view.cpp), so it survives
  // the world camera's own matrix resets untouched. `1` is off. Set once from
  // create() and never changed afterwards, and clamped there so `target`
  // never asks for a texture bigger than max_render_scale_dim on a side.
  i32 render_scale = 1;

  // Text at window resolution. The virtual image is scaled up with a point
  // filter, so text drawn into it is as soft as the scaling is coarse. With a
  // real GPU, screen-space text is queued instead of drawn, and drawn after
  // the scaled image, into the window itself, from an atlas baked at
  // size * scale: sharp at any window size. Text in world space (under the
  // world camera) and text drawn into a render texture stay in the virtual
  // image, where they belong.
  //
  // Queued text would sit above everything in the virtual image, so what is
  // drawn over it afterwards is recorded (view_text_occlude) and applied when
  // the text is drawn: the part of a line under a panel is blended towards the
  // panel's colour, or dropped when the panel is opaque. Draw order is thus
  // kept for rectangles and UI skins; other later draws (sprites, textures) do
  // not cover queued text.
  bool crisp_text = false;             // wanted and possible, set every frame
  mutable i32 world_depth = 0;         // inside begin/end of the world camera
  mutable i32 offscreen_depth = 0;     // inside a render texture
  mutable rect clip{};                 // clip_begin() area; size 0 when none

  // Smooth UI (config::smooth_ui): after the world, the virtual image is put
  // on the window and the rest of the frame is drawn straight into the window,
  // under a scale-and-offset transform that maps virtual pixels to window ones.
  // Shapes are then rasterized at window resolution, and text is baked at
  // size * scale and drawn without the transform, so it is sharp.
  bool smooth_ui = false;              // wanted, set every frame
  mutable bool ui_window = false;      // the UI pass is running
  mutable std::vector<queued_text> text_layer;
  mutable std::vector<text_occluder> occluders;

  view_state() = default;
  ~view_state();
  view_state(const view_state &) = delete;
  view_state &operator=(const view_state &) = delete;
};

// An explicit virtual size was set (window_set_virtual_size()), as opposed to
// supersampling alone driving the offscreen target.
inline bool view_has_virtual_size(const view_state &view) {
  return view.size.x >= 1.0f && view.size.y >= 1.0f;
}

inline bool view_active(const view_state &view) {
  return view_has_virtual_size(view) || view.render_scale > 1;
}

// What the game sees as the screen: the virtual size when set, else the real
// window. screen_size(), the mouse and the camera all use this, never
// `view.target`'s own (possibly bigger, with render_scale) pixel size.
inline vec2 view_logical_size(const view_state &view) {
  if (view_has_virtual_size(view))
    return view.size;
  return {(f32)GetScreenWidth(), (f32)GetScreenHeight()};
}

// True when a screen-space draw_text right now goes to the window-resolution
// pass instead of the virtual image.
inline bool view_text_deferred(const view_state &view) {
  return view.crisp_text && view.drawing && view.scale != 1.0f && view.world_depth == 0 &&
         view.offscreen_depth == 0;
}
// A rectangle of `color` was just drawn over the virtual image: text queued
// before it, and under it, is blended towards `color`, or dropped when `hide`.
// Does nothing unless text is being deferred.
void view_text_occlude(const view_state &view, rect area, rgba color, bool hide = false);

// Ends the virtual image (it is put on the window) and starts the UI pass when
// smooth UI is wanted and the image is scaled. Returns whether it did.
bool view_ui_begin(view_state &view);
// Ends the UI pass.
void view_ui_end(view_state &view);
// Leaves the UI pass's transform for the moment, before the game starts a render
// texture: view_rebind() (called when it ends) puts it back.
void view_ui_suspend(const view_state &view);

// Clamps a requested config::render_scale so `window_size * requested`
// never exceeds a GL texture side every desktop GPU can create. Called once
// from create(), against the just-opened window's real size.
i32 view_clamp_render_scale(i32 requested, vec2 window_size);

// Binds `target` for drawing at `logical` coordinates, `target` itself
// possibly bigger (config::render_scale). See njin_view.cpp for why this is
// not just BeginTextureMode(target). Used by view_draw_begin()/view_rebind()
// for the virtual/window screen, and by camera.cpp for the world's own
// post-processing target, so render_scale covers both.
void bind_view_target(const RenderTexture2D &target, vec2 logical);

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
