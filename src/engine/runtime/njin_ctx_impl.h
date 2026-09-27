#pragma once
#include "njin_internal_only.h"

#include "_random.h"
#include "_types.h"
#include "modules/camera.h"
#include "modules/collision.h"
#include "modules/debug.h"
#include "modules/dialog.h"
#include "modules/fx.h"
#include "modules/particles_gpu.h"
#include "modules/post_fx.h"
#include "modules/reload.h"
#include "modules/render_stats.h"
#include "modules/ui.h"
#include "modules/sprite.h"
#include "modules/timer.h"
#include "njin_anim_impl.h"
#include "njin_audio_impl.h"
#include "njin_cfg.h"
#include "njin_ecs.h"
#include "njin_font.h"
#include "njin_i18n_impl.h"
#include "njin_input_impl.h"
#include "njin_level_impl.h"
#include "njin_prefab_impl.h"
#include "njin_scene_impl.h"
#include "njin_shader.h"
#include "njin_texture.h"
#include "njin_view.h"
#include <string>
#include <vector>

namespace njin {
// Opens the window on construction and closes it on destruction.
struct window_guard {
  explicit window_guard(const njin_cfg &cfg);
  ~window_guard();
  window_guard(const window_guard &) = delete;
  window_guard &operator=(const window_guard &) = delete;
};

// Frame timing. `dt` is what delta() returns outside phase_fixed_update:
// dt_real scaled by `scale`, or 0 while paused.
struct time_state {
  f32 dt = 0.0f;
  f32 dt_real = 0.0f;
  f32 elapsed = 0.0f;
  f32 scale = 1.0f;
  bool paused = false;
  f32 fixed_dt = 1.0f / 60.0f;
  f32 fixed_accum = 0.0f;
  f32 fixed_alpha = 0.0f;
  bool in_fixed = false; // delta() answers fixed_dt while this is set
};

// Definition of the opaque njin_ctx handle. Only the runtime sees this.
//
// Members are destroyed in reverse order, so `window` (declared before the
// stores) closes last: GPU resources in the stores, the camera's post target
// and the tilemap chunk cache, and the audio buffers in `audio`, are freed
// while the GL context and the audio device are still alive. `ecs` goes
// first, so component destructors never outlive the resources they name.
struct njin_ctx {
  explicit njin_ctx(const njin_cfg &config) : cfg(config), window(config) {}

  time_state time;
  bool quit = false;
  // Paths requested by screenshot() this frame, taken at its end.
  std::vector<std::string> screenshots;
  rng random;
  njin_cfg cfg;
  window_guard window;
  view_state view;
  input_store input;
  shader_store shader;
  texture_store texture;
  render_texture_store render_texture;
  // Mutable: atlases are baked on first draw, and drawing takes a const ctx.
  mutable font_store font;
  audio_store audio;
  camera_post post;
  fx_state fx;
  post_chain postfx;
  particle_gpu_state particles_gpu;
  render_stats stats;
  reload_state reload;
  ui_state ui;
  dialog_state dialog;
  i18n_store i18n;
  debug_state debug;
  sprite_cache sprites;
  timer_state timers;
  collision_state collision;
  scene_store scene;
  prefab_store prefab;
  anim_store anim;
  level_store level;
  ecs_store ecs;
};
// Saves every screenshot requested this frame. Called after post_render and
// before EndDrawing, while the back buffer still holds the finished frame.
void take_pending_screenshots(njin_ctx &ctx);
// Draws the text queued for window resolution (view_state::text_layer) onto the
// window, over the scaled virtual image. Called right after view_draw_end.
void text_layer_flush(njin_ctx &ctx);
} // namespace njin
