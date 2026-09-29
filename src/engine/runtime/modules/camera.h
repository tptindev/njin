#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"
#include <raylib.h>

namespace njin {
// Core module. Draws phase_render through the active camera (camera_active):
// begins 2D mode in phase_pre_render and ends it in phase_post_render, so
// post_render is screen space. Also owns camera_active/w2scr/scr2w.
//
// With a post shader set or built-in post effects on (post_fx.h), the world
// is drawn into `target` instead of the screen, run through the effects, and
// then drawn to the screen through the shader, before any post_render system
// runs, so the UI stays untouched.
mod_desc camera_module();

struct camera_post {
  shader_handle shader{};
  RenderTexture2D target{};
  // Set when this frame's world pass went into `target`, so the end of the
  // pass matches its beginning even if the shader changes mid-frame.
  bool drawing = false;
  // The camera this frame's world pass used (shake included), for the light map.
  Camera2D world_camera{};

  camera_post() = default;
  ~camera_post();
  camera_post(const camera_post &) = delete;
  camera_post &operator=(const camera_post &) = delete;
};

// Binds again the image the world pass draws into (the post target, the
// virtual image or the window), after something drew into another one in the
// middle of the pass (render3d's shadow map). Viewport and framebuffer only
// matter to the caller; the matrices are reset to the world pass's.
void world_target_rebind(context &ctx);
} // namespace njin
