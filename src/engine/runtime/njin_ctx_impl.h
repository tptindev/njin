#pragma once
#include "njin_internal_only.h"

#include "_random.h"
#include "_types.h"
#include "modules/camera.h"
#include "modules/collision.h"
#include "modules/debug.h"
#include "modules/dialog.h"
#include "modules/fx.h"
#include "modules/gizmo.h"
#include "modules/lighting.h"
#include "modules/particles_gpu.h"
#include "modules/post_fx.h"
#include "modules/reload.h"
#include "modules/render_stats.h"
#include "modules/ui.h"
#include "modules/sprite.h"
#include "modules/timer.h"
#include "njin_anim_impl.h"
#include "njin_audio_impl.h"
#include "njin_backdrop.h"
#include "njin_cfg.h"
#include "njin_ecs.h"
#include "njin_features.h"
#include "njin_font.h"
#include "njin_i18n_impl.h"
#include "njin_input_impl.h"
#include "njin_level_impl.h"
#include "njin_pace.h"
#include "njin_prefab_impl.h"
#include "njin_quality_impl.h"
#include "njin_scene_impl.h"
#include "njin_sdf2d.h"
#include "njin_shader.h"
#include "njin_texture.h"
#include "njin_view.h"
#include <string>
#include <vector>

namespace njin {
// The optional features' states (njin_features.h): only the feature that owns
// one sees it complete.
struct script_state;
struct instance_store;
struct model_store;
struct physics3d_state;
struct render3d_state;
struct post3d_state;
struct particles3d_state;
struct world3d_store;
struct nav3d_store;
struct video_store;
struct network_store;
struct anim3d_store;

// Opens the window on construction and closes it on destruction.
struct window_guard {
  explicit window_guard(const config &cfg);
  ~window_guard();
  window_guard(const window_guard &) = delete;
  window_guard &operator=(const window_guard &) = delete;
  frame_pacer pacer; // config::low_power only
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
  f32 fixed_rate = 1.0f; // time_set_fixed_rate: fixed steps run this much faster than time
  bool in_fixed = false; // delta() answers fixed_dt while this is set
};

// Definition of the opaque context handle. Only the runtime sees this.
//
// Members are destroyed in reverse order, so `window` (declared before the
// stores) closes last: GPU resources in the stores, the camera's post target
// and the tilemap chunk cache, and the audio buffers in `audio`, are freed
// while the GL context and the audio device are still alive. `ecs` goes
// first, so component destructors never outlive the resources they name.
// The features' states (feature_ref) keep those places: they are made after
// the window opens (features_install), and freed where the member stands.
struct context {
  explicit context(const config &config) : cfg(config), window(config) {}

  // The features' hooks and modules (njin_features.h).
  feature_hooks hooks;

  time_state time;
  bool quit = false;
  // window_set_close_intercept(): closing the window only sets
  // close_requested, for one frame, instead of ending the loop.
  bool close_intercept = false;
  bool close_requested = false;
  // frame_skip_draw(): this frame updates but draws nothing (low_power only).
  bool skip_draw = false;
  // Paths requested by screenshot() this frame, taken at its end.
  std::vector<std::string> screenshots;
  rng random;
  // Early, so the Lua state outlives every store whose timers and tweens may
  // hold Lua callbacks.
  feature_ref<script_state> script;
  config cfg;
  window_guard window;
  view_state view;
  quality_state quality;
  input_store input;
  shader_store shader;
  feature_ref<instance_store> instances;
  texture_store texture;
  render_texture_store render_texture;
  feature_ref<model_store> model;
  feature_ref<physics3d_state> physics3d;
  // Mutable: atlases are baked on first draw, and drawing takes a const ctx.
  mutable font_store font;
  // Mutable too: the shape shader is loaded on first draw (njin_sdf2d.h).
  mutable sdf2d_state sdf2d;
  // And the backdrop blur's shaders and targets (njin_backdrop.h).
  mutable backdrop_state backdrop;
  audio_store audio;
  camera_post post;
  fx_state fx;
  gizmo_state gizmos;
  post_chain postfx;
  lighting_state light;
  feature_ref<render3d_state> render3d;
  feature_ref<post3d_state> post3d;
  feature_ref<particles3d_state> particles3d;
  feature_ref<world3d_store> world3d;
  feature_ref<nav3d_store> nav3d;
  // After texture and audio: closing a video frees its stream and its texture slot.
  feature_ref<video_store> video;
  // Stops the network thread and closes every socket.
  feature_ref<network_store> network;
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
  feature_ref<anim3d_store> anim3d;
  level_store level;
  ecs_store ecs;
};
// Saves every screenshot requested this frame. Called after post_render and
// before EndDrawing, while the back buffer still holds the finished frame.
void take_pending_screenshots(context &ctx);
// Draws the text queued for window resolution (view_state::text_layer) onto the
// window, over the scaled virtual image. Called right after view_draw_end.
void text_layer_flush(context &ctx);
} // namespace njin
