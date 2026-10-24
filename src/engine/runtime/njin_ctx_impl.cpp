// The context's constructor, the one source that sees every core store
// complete, and ctx_ref() of each member (njin_ctx_get.h).
#define NJIN_CTX_DEFINE
#include "njin_ctx_impl.h"

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
#include "modules/sprite.h"
#include "modules/timer.h"
#include "modules/ui.h"
#include "njin_anim_impl.h"
#include "njin_audio_impl.h"
#include "njin_backdrop.h"
#include "njin_ecs.h"
#include "njin_font.h"
#include "njin_i18n_impl.h"
#include "njin_input_impl.h"
#include "njin_level_impl.h"
#include "njin_prefab_impl.h"
#include "njin_quality_impl.h"
#include "njin_scene_impl.h"
#include "njin_sdf2d.h"
#include "njin_shader.h"
#include "njin_texture.h"
#include "njin_view.h"

namespace njin {
context::context(const config &config) : cfg(config), window(config) {
  view.emplace();
  quality.emplace();
  input.emplace();
  shader.emplace();
  texture.emplace();
  render_texture.emplace();
  font.emplace();
  sdf2d.emplace();
  backdrop.emplace();
  audio.emplace();
  post.emplace();
  fx.emplace();
  gizmos.emplace();
  postfx.emplace();
  light.emplace();
  particles_gpu.emplace();
  stats.emplace();
  reload.emplace();
  ui.emplace();
  dialog.emplace();
  i18n.emplace();
  debug.emplace();
  sprites.emplace();
  timers.emplace();
  collision.emplace();
  scene.emplace();
  prefab.emplace();
  anim.emplace();
  level.emplace();
  ecs.emplace();
}

context::~context() = default;

#if !defined(NJIN_CTX_INLINE)
// Made here once for every source of a Debug build (njin_ctx_get.h).
template ctx_slot_t<feature_hooks> &ctx_ref<feature_hooks>(context &);
template const ctx_slot_t<feature_hooks> &ctx_ref<feature_hooks>(const context &);
template ctx_slot_t<time_state> &ctx_ref<time_state>(context &);
template const ctx_slot_t<time_state> &ctx_ref<time_state>(const context &);
template ctx_slot_t<frame_state> &ctx_ref<frame_state>(context &);
template const ctx_slot_t<frame_state> &ctx_ref<frame_state>(const context &);
template ctx_slot_t<rng> &ctx_ref<rng>(context &);
template const ctx_slot_t<rng> &ctx_ref<rng>(const context &);
template ctx_slot_t<config> &ctx_ref<config>(context &);
template const ctx_slot_t<config> &ctx_ref<config>(const context &);
template ctx_slot_t<window_guard> &ctx_ref<window_guard>(context &);
template const ctx_slot_t<window_guard> &ctx_ref<window_guard>(const context &);
template ctx_slot_t<script_state> &ctx_ref<script_state>(context &);
template const ctx_slot_t<script_state> &ctx_ref<script_state>(const context &);
template ctx_slot_t<view_state> &ctx_ref<view_state>(context &);
template const ctx_slot_t<view_state> &ctx_ref<view_state>(const context &);
template ctx_slot_t<quality_state> &ctx_ref<quality_state>(context &);
template const ctx_slot_t<quality_state> &ctx_ref<quality_state>(const context &);
template ctx_slot_t<input_store> &ctx_ref<input_store>(context &);
template const ctx_slot_t<input_store> &ctx_ref<input_store>(const context &);
template ctx_slot_t<shader_store> &ctx_ref<shader_store>(context &);
template const ctx_slot_t<shader_store> &ctx_ref<shader_store>(const context &);
template ctx_slot_t<instance_store> &ctx_ref<instance_store>(context &);
template const ctx_slot_t<instance_store> &ctx_ref<instance_store>(const context &);
template ctx_slot_t<texture_store> &ctx_ref<texture_store>(context &);
template const ctx_slot_t<texture_store> &ctx_ref<texture_store>(const context &);
template ctx_slot_t<render_texture_store> &ctx_ref<render_texture_store>(context &);
template const ctx_slot_t<render_texture_store> &ctx_ref<render_texture_store>(const context &);
template ctx_slot_t<model_store> &ctx_ref<model_store>(context &);
template const ctx_slot_t<model_store> &ctx_ref<model_store>(const context &);
template ctx_slot_t<physics3d_state> &ctx_ref<physics3d_state>(context &);
template const ctx_slot_t<physics3d_state> &ctx_ref<physics3d_state>(const context &);
template ctx_slot_t<font_store> &ctx_ref<font_store>(context &);
template const ctx_slot_t<font_store> &ctx_ref<font_store>(const context &);
template ctx_slot_t<sdf2d_state> &ctx_ref<sdf2d_state>(context &);
template const ctx_slot_t<sdf2d_state> &ctx_ref<sdf2d_state>(const context &);
template ctx_slot_t<backdrop_state> &ctx_ref<backdrop_state>(context &);
template const ctx_slot_t<backdrop_state> &ctx_ref<backdrop_state>(const context &);
template ctx_slot_t<audio_store> &ctx_ref<audio_store>(context &);
template const ctx_slot_t<audio_store> &ctx_ref<audio_store>(const context &);
template ctx_slot_t<camera_post> &ctx_ref<camera_post>(context &);
template const ctx_slot_t<camera_post> &ctx_ref<camera_post>(const context &);
template ctx_slot_t<fx_state> &ctx_ref<fx_state>(context &);
template const ctx_slot_t<fx_state> &ctx_ref<fx_state>(const context &);
template ctx_slot_t<gizmo_state> &ctx_ref<gizmo_state>(context &);
template const ctx_slot_t<gizmo_state> &ctx_ref<gizmo_state>(const context &);
template ctx_slot_t<post_chain> &ctx_ref<post_chain>(context &);
template const ctx_slot_t<post_chain> &ctx_ref<post_chain>(const context &);
template ctx_slot_t<lighting_state> &ctx_ref<lighting_state>(context &);
template const ctx_slot_t<lighting_state> &ctx_ref<lighting_state>(const context &);
template ctx_slot_t<render3d_state> &ctx_ref<render3d_state>(context &);
template const ctx_slot_t<render3d_state> &ctx_ref<render3d_state>(const context &);
template ctx_slot_t<post3d_state> &ctx_ref<post3d_state>(context &);
template const ctx_slot_t<post3d_state> &ctx_ref<post3d_state>(const context &);
template ctx_slot_t<particles3d_state> &ctx_ref<particles3d_state>(context &);
template const ctx_slot_t<particles3d_state> &ctx_ref<particles3d_state>(const context &);
template ctx_slot_t<world3d_store> &ctx_ref<world3d_store>(context &);
template const ctx_slot_t<world3d_store> &ctx_ref<world3d_store>(const context &);
template ctx_slot_t<nav3d_store> &ctx_ref<nav3d_store>(context &);
template const ctx_slot_t<nav3d_store> &ctx_ref<nav3d_store>(const context &);
template ctx_slot_t<video_store> &ctx_ref<video_store>(context &);
template const ctx_slot_t<video_store> &ctx_ref<video_store>(const context &);
template ctx_slot_t<network_store> &ctx_ref<network_store>(context &);
template const ctx_slot_t<network_store> &ctx_ref<network_store>(const context &);
template ctx_slot_t<particle_gpu_state> &ctx_ref<particle_gpu_state>(context &);
template const ctx_slot_t<particle_gpu_state> &ctx_ref<particle_gpu_state>(const context &);
template ctx_slot_t<render_stats> &ctx_ref<render_stats>(context &);
template const ctx_slot_t<render_stats> &ctx_ref<render_stats>(const context &);
template ctx_slot_t<reload_state> &ctx_ref<reload_state>(context &);
template const ctx_slot_t<reload_state> &ctx_ref<reload_state>(const context &);
template ctx_slot_t<ui_state> &ctx_ref<ui_state>(context &);
template const ctx_slot_t<ui_state> &ctx_ref<ui_state>(const context &);
template ctx_slot_t<dialog_state> &ctx_ref<dialog_state>(context &);
template const ctx_slot_t<dialog_state> &ctx_ref<dialog_state>(const context &);
template ctx_slot_t<i18n_store> &ctx_ref<i18n_store>(context &);
template const ctx_slot_t<i18n_store> &ctx_ref<i18n_store>(const context &);
template ctx_slot_t<debug_state> &ctx_ref<debug_state>(context &);
template const ctx_slot_t<debug_state> &ctx_ref<debug_state>(const context &);
template ctx_slot_t<sprite_cache> &ctx_ref<sprite_cache>(context &);
template const ctx_slot_t<sprite_cache> &ctx_ref<sprite_cache>(const context &);
template ctx_slot_t<timer_state> &ctx_ref<timer_state>(context &);
template const ctx_slot_t<timer_state> &ctx_ref<timer_state>(const context &);
template ctx_slot_t<collision_state> &ctx_ref<collision_state>(context &);
template const ctx_slot_t<collision_state> &ctx_ref<collision_state>(const context &);
template ctx_slot_t<scene_store> &ctx_ref<scene_store>(context &);
template const ctx_slot_t<scene_store> &ctx_ref<scene_store>(const context &);
template ctx_slot_t<prefab_store> &ctx_ref<prefab_store>(context &);
template const ctx_slot_t<prefab_store> &ctx_ref<prefab_store>(const context &);
template ctx_slot_t<anim_store> &ctx_ref<anim_store>(context &);
template const ctx_slot_t<anim_store> &ctx_ref<anim_store>(const context &);
template ctx_slot_t<anim3d_store> &ctx_ref<anim3d_store>(context &);
template const ctx_slot_t<anim3d_store> &ctx_ref<anim3d_store>(const context &);
template ctx_slot_t<level_store> &ctx_ref<level_store>(context &);
template const ctx_slot_t<level_store> &ctx_ref<level_store>(const context &);
template ctx_slot_t<ecs_store> &ctx_ref<ecs_store>(context &);
template const ctx_slot_t<ecs_store> &ctx_ref<ecs_store>(const context &);
template ctx_slot_t<font_store> &ctx_mut<font_store>(const context &);
template ctx_slot_t<sdf2d_state> &ctx_mut<sdf2d_state>(const context &);
template ctx_slot_t<backdrop_state> &ctx_mut<backdrop_state>(const context &);
#endif
} // namespace njin
