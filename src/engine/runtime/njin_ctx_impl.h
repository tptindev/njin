#pragma once
#include "njin_internal_only.h"

#include "_random.h"
#include "_types.h"
#include "njin_cfg.h"
#include "njin_features.h"
#include "njin_pace.h"
#include <string>
#include <vector>

namespace njin {
// The core's stores, each held in a store_ref: a source sees complete only
// the ones it includes the header of, so editing one module's header
// rebuilds the sources that use it instead of the whole runtime.
struct anim_store;
struct audio_store;
struct backdrop_state;
struct camera_post;
struct collision_state;
struct debug_state;
struct dialog_state;
struct ecs_store;
struct font_store;
struct fx_state;
struct gizmo_state;
struct i18n_store;
struct input_store;
struct level_store;
struct lighting_state;
struct particle_gpu_state;
struct post_chain;
struct prefab_store;
struct quality_state;
struct reload_state;
struct render_stats;
struct render_texture_store;
struct scene_store;
struct sdf2d_state;
struct shader_store;
struct sprite_cache;
struct texture_store;
struct timer_state;
struct ui_state;
struct view_state;

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
  // Makes every store, in the order they are declared (njin_ctx.cpp).
  explicit context(const config &config);
  ~context();

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
  store_ref<view_state> view;
  store_ref<quality_state> quality;
  store_ref<input_store> input;
  store_ref<shader_store> shader;
  feature_ref<instance_store> instances;
  store_ref<texture_store> texture;
  store_ref<render_texture_store> render_texture;
  feature_ref<model_store> model;
  feature_ref<physics3d_state> physics3d;
  // Mutable: atlases are baked on first draw, and drawing takes a const ctx.
  mutable store_ref<font_store> font;
  // Mutable too: the shape shader is loaded on first draw (njin_sdf2d.h).
  mutable store_ref<sdf2d_state> sdf2d;
  // And the backdrop blur's shaders and targets (njin_backdrop.h).
  mutable store_ref<backdrop_state> backdrop;
  store_ref<audio_store> audio;
  store_ref<camera_post> post;
  store_ref<fx_state> fx;
  store_ref<gizmo_state> gizmos;
  store_ref<post_chain> postfx;
  store_ref<lighting_state> light;
  feature_ref<render3d_state> render3d;
  feature_ref<post3d_state> post3d;
  feature_ref<particles3d_state> particles3d;
  feature_ref<world3d_store> world3d;
  feature_ref<nav3d_store> nav3d;
  // After texture and audio: closing a video frees its stream and its texture slot.
  feature_ref<video_store> video;
  // Stops the network thread and closes every socket.
  feature_ref<network_store> network;
  store_ref<particle_gpu_state> particles_gpu;
  store_ref<render_stats> stats;
  store_ref<reload_state> reload;
  store_ref<ui_state> ui;
  store_ref<dialog_state> dialog;
  store_ref<i18n_store> i18n;
  store_ref<debug_state> debug;
  store_ref<sprite_cache> sprites;
  store_ref<timer_state> timers;
  store_ref<collision_state> collision;
  store_ref<scene_store> scene;
  store_ref<prefab_store> prefab;
  store_ref<anim_store> anim;
  feature_ref<anim3d_store> anim3d;
  store_ref<level_store> level;
  store_ref<ecs_store> ecs;
};
// Saves every screenshot requested this frame. Called after post_render and
// before EndDrawing, while the back buffer still holds the finished frame.
void take_pending_screenshots(context &ctx);
// Draws the text queued for window resolution (view_state::text_layer) onto the
// window, over the scaled virtual image. Called right after view_draw_end.
void text_layer_flush(context &ctx);
} // namespace njin
