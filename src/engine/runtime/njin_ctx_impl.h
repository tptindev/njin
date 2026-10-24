#pragma once
#include "njin_internal_only.h"

#include "_random.h"
#include "_types.h"
#include "njin_cfg.h"
#include "njin_ctx_get.h"
#include "njin_features.h"
#include "njin_frame_impl.h"

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
  frame_state frame;
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
// Which member holds a T, for ctx_ref (njin_ctx_get.h).
template <class T> struct ctx_member;
template <> struct ctx_member<feature_hooks> {
  static constexpr auto ptr = &context::hooks;
};
template <> struct ctx_member<time_state> {
  static constexpr auto ptr = &context::time;
};
template <> struct ctx_member<frame_state> {
  static constexpr auto ptr = &context::frame;
};
template <> struct ctx_member<rng> {
  static constexpr auto ptr = &context::random;
};
template <> struct ctx_member<config> {
  static constexpr auto ptr = &context::cfg;
};
template <> struct ctx_member<window_guard> {
  static constexpr auto ptr = &context::window;
};
template <> struct ctx_member<script_state> {
  static constexpr auto ptr = &context::script;
};
template <> struct ctx_member<view_state> {
  static constexpr auto ptr = &context::view;
};
template <> struct ctx_member<quality_state> {
  static constexpr auto ptr = &context::quality;
};
template <> struct ctx_member<input_store> {
  static constexpr auto ptr = &context::input;
};
template <> struct ctx_member<shader_store> {
  static constexpr auto ptr = &context::shader;
};
template <> struct ctx_member<instance_store> {
  static constexpr auto ptr = &context::instances;
};
template <> struct ctx_member<texture_store> {
  static constexpr auto ptr = &context::texture;
};
template <> struct ctx_member<render_texture_store> {
  static constexpr auto ptr = &context::render_texture;
};
template <> struct ctx_member<model_store> {
  static constexpr auto ptr = &context::model;
};
template <> struct ctx_member<physics3d_state> {
  static constexpr auto ptr = &context::physics3d;
};
template <> struct ctx_member<font_store> {
  static constexpr auto ptr = &context::font;
};
template <> struct ctx_member<sdf2d_state> {
  static constexpr auto ptr = &context::sdf2d;
};
template <> struct ctx_member<backdrop_state> {
  static constexpr auto ptr = &context::backdrop;
};
template <> struct ctx_member<audio_store> {
  static constexpr auto ptr = &context::audio;
};
template <> struct ctx_member<camera_post> {
  static constexpr auto ptr = &context::post;
};
template <> struct ctx_member<fx_state> {
  static constexpr auto ptr = &context::fx;
};
template <> struct ctx_member<gizmo_state> {
  static constexpr auto ptr = &context::gizmos;
};
template <> struct ctx_member<post_chain> {
  static constexpr auto ptr = &context::postfx;
};
template <> struct ctx_member<lighting_state> {
  static constexpr auto ptr = &context::light;
};
template <> struct ctx_member<render3d_state> {
  static constexpr auto ptr = &context::render3d;
};
template <> struct ctx_member<post3d_state> {
  static constexpr auto ptr = &context::post3d;
};
template <> struct ctx_member<particles3d_state> {
  static constexpr auto ptr = &context::particles3d;
};
template <> struct ctx_member<world3d_store> {
  static constexpr auto ptr = &context::world3d;
};
template <> struct ctx_member<nav3d_store> {
  static constexpr auto ptr = &context::nav3d;
};
template <> struct ctx_member<video_store> {
  static constexpr auto ptr = &context::video;
};
template <> struct ctx_member<network_store> {
  static constexpr auto ptr = &context::network;
};
template <> struct ctx_member<particle_gpu_state> {
  static constexpr auto ptr = &context::particles_gpu;
};
template <> struct ctx_member<render_stats> {
  static constexpr auto ptr = &context::stats;
};
template <> struct ctx_member<reload_state> {
  static constexpr auto ptr = &context::reload;
};
template <> struct ctx_member<ui_state> {
  static constexpr auto ptr = &context::ui;
};
template <> struct ctx_member<dialog_state> {
  static constexpr auto ptr = &context::dialog;
};
template <> struct ctx_member<i18n_store> {
  static constexpr auto ptr = &context::i18n;
};
template <> struct ctx_member<debug_state> {
  static constexpr auto ptr = &context::debug;
};
template <> struct ctx_member<sprite_cache> {
  static constexpr auto ptr = &context::sprites;
};
template <> struct ctx_member<timer_state> {
  static constexpr auto ptr = &context::timers;
};
template <> struct ctx_member<collision_state> {
  static constexpr auto ptr = &context::collision;
};
template <> struct ctx_member<scene_store> {
  static constexpr auto ptr = &context::scene;
};
template <> struct ctx_member<prefab_store> {
  static constexpr auto ptr = &context::prefab;
};
template <> struct ctx_member<anim_store> {
  static constexpr auto ptr = &context::anim;
};
template <> struct ctx_member<anim3d_store> {
  static constexpr auto ptr = &context::anim3d;
};
template <> struct ctx_member<level_store> {
  static constexpr auto ptr = &context::level;
};
template <> struct ctx_member<ecs_store> {
  static constexpr auto ptr = &context::ecs;
};

// ctx_ref's definitions: inline in every source (NJIN_CTX_INLINE), or made once
// in njin_ctx_impl.cpp.
#if defined(NJIN_CTX_INLINE) || defined(NJIN_CTX_DEFINE)
template <class T> NJIN_CTX_FN ctx_slot_t<T> &ctx_ref(context &ctx) { return ctx.*ctx_member<T>::ptr; }
template <class T> NJIN_CTX_FN const ctx_slot_t<T> &ctx_ref(const context &ctx) { return ctx.*ctx_member<T>::ptr; }
template <class T> NJIN_CTX_FN ctx_slot_t<T> &ctx_mut(const context &ctx) {
  return const_cast<context &>(ctx).*ctx_member<T>::ptr;
}
#endif
} // namespace njin
