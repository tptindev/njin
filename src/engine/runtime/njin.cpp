#include "njin_audio_backend.h"
#include "_math.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_stream_internal.h"
#include "njin_input.h"
#include "njin_log.h"
#include "njin_particles.h"
#include "njin_post.h"
#include "njin_ui.h"
#include "njin_version.h"
#include "njin_window.h"
#include "modules/core_modules.h"
#include "modules/fx.h"
#include "modules/gizmo.h"
#include "njin2rl.h"
#include "njin_gpu_caps.h"
#include "njin_gpu_hint.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_bgfx.h"
#include "njin_texture.h"
#include "njin_shader.h"
#include "modules/debug.h"
#include "modules/reload.h"
#include "modules/ui.h"
#include "modules/hierarchy.h"
#include "modules/timer.h"
#include "modules/sprite.h"
#include "modules/particles.h"
#include "modules/audio.h"
#include "modules/follow.h"
#include "modules/anim.h"
#include "modules/body.h"
#include "modules/collision.h"
#include "modules/dialog.h"
#include "njin_scene_impl.h"
#include "modules/post_fx.h"
#include "modules/lighting.h"
#include "njin_ecs.h"
#include "njin_smoke.h"
#include "njin_input_impl.h"
#include "njin_mem_impl.h"
#include "njin_log_impl.h"
#include "njin_view.h"
#include <thread>
#include "njin_ctx_impl.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <stdexcept>
#include "njin_rl.h"

namespace njin {
namespace {
// At most config::fixed_max_steps fixed steps per frame. After a long stall (a
// breakpoint, dragging the window) the backlog is dropped instead of being
// simulated in one frame, which would only make the next frame longer still.
void run_fixed_steps(context &ctx) {
  const i32 fixed_max_steps = std::max(ctx_ref<config>(ctx).fixed_max_steps, 1);
  time_state &time = ctx_ref<time_state>(ctx);
  if (time.paused) {
    time.fixed_alpha = time.fixed_dt > 0.0f ? time.fixed_accum / time.fixed_dt : 0.0f;
    return;
  }
  time.fixed_accum += time.dt * time.fixed_rate;
  i32 steps = 0;
  time.in_fixed = true;
  while (time.fixed_accum >= time.fixed_dt && steps < fixed_max_steps) {
    ecs_run(ctx, phase_fixed_update);
    // Bodies and characters move right after the game set them in this step.
    if (ctx_ref<feature_hooks>(ctx).fixed_step != nullptr)
      ctx_ref<feature_hooks>(ctx).fixed_step(ctx, time.fixed_dt);
    time.fixed_accum -= time.fixed_dt;
    steps++;
  }
  time.in_fixed = false;
  if (steps == fixed_max_steps && time.fixed_accum >= time.fixed_dt)
    time.fixed_accum = 0.0f;
  time.fixed_alpha = time.fixed_accum / time.fixed_dt;
}
} // namespace

window_guard::window_guard(const config &cfg) {
#if NJIN_BGFX
  if (cfg.graphics_backend != GraphicsBackend::OpenGL) {
    platform=std::make_unique<SdlPlatform>(cfg);
    graphics=rhi::make_bgfx_device(*platform,cfg);
    pacer_open(pacer);
    if (cfg.audio) audio_device_open();
    return;
  }
#endif
  log_capture_raylib();
  unsigned int flags = 0;
  if (cfg.resizable)
    flags |= FLAG_WINDOW_RESIZABLE;
  if (cfg.vsync)
    flags |= FLAG_VSYNC_HINT;
  if (!cfg.decorated)
    flags |= FLAG_WINDOW_UNDECORATED;
  if (cfg.transparent)
    flags |= FLAG_WINDOW_TRANSPARENT;
  SetConfigFlags(flags);
  graphics=rhi::make_raylib_device(renderer_resources);
  InitWindow((i32)cfg.width, (i32)cfg.height, cfg.title);
  if(!IsWindowReady()) throw std::runtime_error("njin OpenGL: window/context initialization failed");
  // Without a device (no speakers, driver problem) the game still runs; the
  // audio calls just fail to load and say so in the log. A game that plays no
  // sound opens none (config::audio).
  if (cfg.audio)
    audio_device_open();
  // low_power: run() paces the frames itself, sleeping the whole wait.
  SetTargetFPS(cfg.low_power ? 0 : (i32)cfg.target_fps);
  if (cfg.low_power)
    pacer_open(pacer);
  i32 exit_key = KEY_NULL;
  if (cfg.exit_key != key_none)
    to_raylib(cfg.exit_key, exit_key);
  SetExitKey(exit_key);
}

window_guard::~window_guard() {
  for(auto &[id,mesh]:renderer_resources.meshes) if(mesh.gl.vertices) UnloadMesh(mesh.gl);
  renderer_resources.meshes.clear();
  // Members normally die after this body: release GL resources while the
  // window/context is still alive.
  for(auto &[id,texture]:renderer_resources.textures) if(texture.gl.id && !texture.target) UnloadTexture(texture.gl);
  renderer_resources.textures.clear();
  for(auto &[id,target]:renderer_resources.targets) if(target.gl.id) UnloadRenderTexture(target.gl);
  renderer_resources.targets.clear();
  pacer_close(pacer);
  audio_device_close();
  const bool opengl=!platform;
  graphics.reset(); platform.reset();
  if(opengl) CloseWindow();
}

context *create(const config &game_cfg) {
  const config cfg = smoke_config(game_cfg); // NJIN_SMOKE_BACKEND (test suite only)
  if(static_cast<unsigned>(cfg.graphics_backend)>static_cast<unsigned>(GraphicsBackend::WebGL2))
    throw std::invalid_argument("njin: invalid graphics backend");
#if !NJIN_BGFX
  if (cfg.graphics_backend != GraphicsBackend::OpenGL)
    throw std::runtime_error("njin: bgfx backend was not built; configure NJIN_BGFX=ON");
#endif
  prefer_discrete_gpu();
  // Keep the log lines from here (the window's own GL and audio messages) for an
  // inspector that a game may attach before run; run lets go of them.
  log_hold(true);
  mem_apply_config(cfg);
  auto owned = std::make_unique<context>(cfg);
  context *ctx = owned.get();
  ctx_ref<time_state>(*ctx).fixed_dt = cfg.fixed_hz > 0.0f ? 1.0f / cfg.fixed_hz : 1.0f / 60.0f;
  ctx_ref<rng>(*ctx).reseed(
      (u64)std::chrono::high_resolution_clock::now().time_since_epoch().count());
  if (cfg.graphics_backend != GraphicsBackend::OpenGL) {
    // The legacy render modules require rlgl. Register only portable systems
    // here until those modules have renderer adapters.
    ctx_ref<texture_store>(*ctx)->portable_context=ctx;
    ctx_ref<shader_store>(*ctx)->portable_context=ctx;
    auto &v=ctx_ref<view_state>(*ctx);v->portable_context=ctx;
    if(cfg.virtual_size.x>=1&&cfg.virtual_size.y>=1) window_set_virtual_size(*ctx,cfg.virtual_size,cfg.integer_scale);
    v->render_scale=view_clamp_render_scale(cfg.render_scale,view_logical_size(v));
    v->smooth_ui=cfg.smooth_ui;v->crisp_text=cfg.crisp_text;
    features_install(*ctx);
    mod_register(*ctx, {reload_module(),debug_module(),ui_module(),dialog_module(),audio_module(),hierarchy_module(),camera_follow_module(),anim_module(),particles_module()});
    for(const auto &module:ctx_ref<feature_hooks>(*ctx).modules_mid) mod_register(*ctx,module);
    mod_register(*ctx, {sprite_module(),collision_module(),body_module(),timer_module()});
    for(const auto &module:ctx_ref<feature_hooks>(*ctx).modules_last) mod_register(*ctx,module);
    smoke_install(*ctx);
    return owned.release();
  }
  ctx_ref<time_state>(*ctx).fixed_dt = cfg.fixed_hz > 0.0f ? 1.0f / cfg.fixed_hz : 1.0f / 60.0f;
  if (cfg.virtual_size.x >= 1.0f && cfg.virtual_size.y >= 1.0f)
    window_set_virtual_size(*ctx, cfg.virtual_size, cfg.integer_scale);
  ctx_ref<view_state>(*ctx)->render_scale =
      view_clamp_render_scale(cfg.render_scale, {(f32)GetScreenWidth(), (f32)GetScreenHeight()});
  if (ctx_ref<view_state>(*ctx)->render_scale != cfg.render_scale && cfg.render_scale > 1)
    NJIN_WARN("config::render_scale %d at the window's size would need a texture "
             "bigger than any GPU guarantees; using %dx instead",
             cfg.render_scale, ctx_ref<view_state>(*ctx)->render_scale);
  // The game's features (njin_features.h): their states, hooks and modules,
  // before the core modules register theirs among them.
  features_install(*ctx);
  register_core_modules(*ctx);
  smoke_install(*ctx);
  return owned.release();
}

// The version of the linked library. Built from the same macros as the header,
// so a game built against another header shows a different number.
const char *version() {
  static const std::string text = std::to_string(NJIN_VERSION_MAJOR) + "." + std::to_string(NJIN_VERSION_MINOR) +
                                  "." + std::to_string(NJIN_VERSION_PATCH);
  return text.c_str();
}

void run(context &ctx) {
#if NJIN_BGFX
  if (auto *gpu = ctx_ref<window_guard>(ctx).platform.get()) {
    if (ctx_ref<ecs_store>(ctx)->started) return;
    ctx_ref<ecs_store>(ctx)->started = true;
    log_hold(false);
    NJIN_WARN("bgfx path: portable ECS sprite/camera adapter enabled; advanced legacy rendering is unavailable");
    ecs_run(ctx, phase_startup);
    const auto start = std::chrono::steady_clock::now();
    auto previous = start;
    while (!ctx_ref<frame_state>(ctx).quit) {
      streaming_update(ctx);
      const bool window_close=gpu->poll_close();
      const auto now = std::chrono::steady_clock::now();
      auto &time = ctx_ref<time_state>(ctx);
      time.dt_real = std::chrono::duration<float>(now - previous).count();
      time.elapsed = std::chrono::duration<float>(now - start).count();
      time.dt = time.paused ? 0 : time.dt_real * time.scale;
      fx_frame_begin(ctx);
      gizmo_frame_begin(ctx);
      if(ctx_ref<ecs_store>(ctx)->profile) ecs_profile_roll(ctx_ref<ecs_store>(ctx));
      mem_frame(ctx);
      previous = now;
      view_frame_begin(ctx_ref<view_state>(ctx),window_size(ctx));
      gpu->poll_input(ctx_ref<input_store>(ctx),&*ctx_ref<view_state>(ctx));
      view_map_mouse(ctx_ref<view_state>(ctx),ctx_ref<input_store>(ctx)->cur.mouse_pos,ctx_ref<input_store>(ctx)->cur.mouse_delta);
      auto &frame=ctx_ref<frame_state>(ctx);
      const auto exit=ctx_ref<config>(ctx).exit_key;
      frame.close_requested=window_close || key_pressed(ctx,exit);
      if(frame.close_requested && !frame.close_intercept) break;
      scene_store_apply(ctx);
      ecs_run(ctx, phase_pre_update);
      run_fixed_steps(ctx);
      ecs_run(ctx, phase_update);
      ecs_run(ctx, phase_post_update);
      ctx_ref<ecs_store>(ctx)->dispatcher.update();
      const auto updated=std::chrono::steady_clock::now();
      frame.cpu_update_ms=std::chrono::duration<float,std::milli>(updated-now).count();
      if(ctx_ref<config>(ctx).low_power && frame.skip_draw) {
        frame.skip_draw=false;
        frame.cpu_render_ms=frame.cpu_present_ms=0;
        const auto fps=ctx_ref<config>(ctx).target_fps;
        if(fps>0) pacer_sleep(ctx_ref<window_guard>(ctx).pacer,1.0/fps-std::chrono::duration<double>(std::chrono::steady_clock::now()-now).count());
        continue;
      }
      frame.skip_draw=false;
      if(!ctx_ref<window_guard>(ctx).platform->native_handle()) {
        // SDL can temporarily detach the Android surface during lifecycle
        // transitions. Keep pumping events rather than submitting to it.
        frame.cpu_render_ms=frame.cpu_present_ms=0;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      ctx_ref<window_guard>(ctx).graphics->begin_frame(view_active(ctx_ref<view_state>(ctx))?ctx_ref<view_state>(ctx)->bars:ctx_ref<config>(ctx).clear_bg_color);
      ctx_ref<window_guard>(ctx).renderer_resources.pass_dirty=false;
      portable_view_draw_begin(ctx);
      ctx_ref<view_state>(ctx)->world_depth=1;
      ecs_run(ctx, phase_pre_render);
      ecs_run(ctx, phase_render);
      if(auto close=ctx_ref<feature_hooks>(ctx).world_end) close(ctx);
      ctx_ref<view_state>(ctx)->world_depth=0;
      portable_lighting_world(ctx);
      portable_post_world(ctx);
      portable_camera_post(ctx);
      portable_view_ui_begin(ctx);
      gizmo_draw_screen(ctx);
      ecs_run(ctx, phase_post_render);
      dialog_draw(ctx);
      ui_canvas_begin(ctx);
      ui_draw_toasts(ctx);
      ui_canvas_end(ctx);
      fx_draw_screen_flash(ctx);
      scene_fade_draw(ctx);
      portable_view_draw_end(ctx);ctx_ref<view_state>(ctx)->ui_window=false;
      text_layer_flush(ctx);
      const auto rendered=std::chrono::steady_clock::now();
      frame.cpu_render_ms=std::chrono::duration<float,std::milli>(rendered-updated).count();
      RenderImageAsset capture;const bool recording=debug_record_capture_due(ctx);
      ctx_ref<window_guard>(ctx).graphics->end_frame(frame.screenshots.empty()&&!recording?nullptr:&capture);
      save_pending_capture(ctx,capture);
      if(recording) debug_record_capture(ctx,capture.view());
      frame.cpu_present_ms=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-rendered).count();
      const auto fps=ctx_ref<config>(ctx).target_fps;
      if(fps>0) pacer_sleep(ctx_ref<window_guard>(ctx).pacer,1.0/fps-std::chrono::duration<double>(std::chrono::steady_clock::now()-now).count());
    }
    ecs_run(ctx, phase_shutdown);
    return;
  }
#endif
  if (ctx_ref<ecs_store>(ctx)->started) {
    return;
  }
  ctx_ref<ecs_store>(ctx)->started = true;
  log_hold(false);
  NJIN_INFO("njin %s", version());
  // Driver shader compiles cost tens of milliseconds each: pay them here, not
  // on the first hit, pause or explosion. Not in low_power: there each is
  // loaded on its first use, and one never used costs no memory.
  if (!ctx_ref<config>(ctx).low_power) {
    fx_warmup(ctx);
    post_chain_warmup(ctx);
    particles_gpu_available(ctx);
  }
  ecs_run(ctx, phase_startup);

  Color clearbg = RAYWHITE;
  to_raylib(ctx_ref<config>(ctx).clear_bg_color, clearbg);
  const bool low_power = ctx_ref<config>(ctx).low_power;
  double frame_start = GetTime();
  while (!ctx_ref<frame_state>(ctx).quit) {
    // raylib holds a close (the [x], Alt+F4, the exit key) for one frame.
    ctx_ref<frame_state>(ctx).close_requested = WindowShouldClose();
    if (ctx_ref<frame_state>(ctx).close_requested && !ctx_ref<frame_state>(ctx).close_intercept)
      break;
    time_state &time = ctx_ref<time_state>(ctx);
    if (low_power) {
      // raylib runs uncapped here, so its frame time leaves out the sleep.
      const double now = GetTime();
      time.dt_real = (f32)(now - frame_start);
      frame_start = now;
    } else {
      time.dt_real = GetFrameTime();
    }
    time.elapsed = (f32)GetTime();
    const double stage_start = GetTime();
    time.dt = time.paused ? 0.0f : time.dt_real * time.scale;
    // Hitstop zeroes dt here, before any system (or fixed step) reads it.
    fx_frame_begin(ctx);
    mem_frame(ctx);
    gizmo_frame_begin(ctx);
    if (ctx_ref<ecs_store>(ctx)->profile)
      ecs_profile_roll(ctx_ref<ecs_store>(ctx));
    view_frame_begin(ctx_ref<view_state>(ctx));
    input_key_poll(ctx_ref<input_store>(ctx));
    view_map_mouse(ctx_ref<view_state>(ctx), ctx_ref<input_store>(ctx)->cur.mouse_pos, ctx_ref<input_store>(ctx)->cur.mouse_delta);
    touch_poll(ctx_ref<input_store>(ctx)->touch, ctx_ref<view_state>(ctx), GetWindowHandle(), GetTime());
    // A scene switch requested last frame happens here, before any system
    // of the new frame runs.
    scene_store_apply(ctx);

    ecs_run(ctx, phase_pre_update);
    run_fixed_steps(ctx);
    ecs_run(ctx, phase_update);
    ecs_run(ctx, phase_post_update);
    ctx_ref<ecs_store>(ctx)->dispatcher.update();
    const double updated = GetTime();
    ctx_ref<frame_state>(ctx).cpu_update_ms = (f32)((updated - stage_start) * 1000.0);

    if (low_power && ctx_ref<frame_state>(ctx).skip_draw) {
      // Nothing changed on screen: the window keeps its last image.
      ctx_ref<frame_state>(ctx).skip_draw = false;
      ctx_ref<frame_state>(ctx).cpu_render_ms = ctx_ref<frame_state>(ctx).cpu_present_ms = 0.0f;
      PollInputEvents();
      pacer_sleep(ctx_ref<window_guard>(ctx).pacer, frame_start + 1.0 / ctx_ref<config>(ctx).target_fps - GetTime());
      continue;
    }
    ctx_ref<frame_state>(ctx).skip_draw = false;
    ctx_ref<window_guard>(ctx).graphics->begin_frame(ctx_ref<config>(ctx).clear_bg_color);
    ctx_ref<view_state>(ctx)->crisp_text = ctx_ref<config>(ctx).crisp_text && !gpu_is_software();
    ctx_ref<view_state>(ctx)->smooth_ui = ctx_ref<config>(ctx).smooth_ui;
    view_draw_begin(ctx_ref<view_state>(ctx), clearbg);
    ecs_run(ctx, phase_pre_render);
    ecs_run(ctx, phase_render);
    ecs_run(ctx, phase_post_render);
    // Over everything, UI included: toasts, the screen flash, then the scene
    // fade, which covers the whole frame.
    dialog_draw(ctx);
    ui_canvas_begin(ctx); // toasts are UI: in the canvas when there is one
    ui_draw_toasts(ctx);
    ui_canvas_end(ctx);
    fx_draw_screen_flash(ctx);
    scene_fade_draw(ctx);
    if (ctx_ref<view_state>(ctx)->ui_window)
      view_ui_end(ctx_ref<view_state>(ctx));
    else
      view_draw_end(ctx_ref<view_state>(ctx));
    text_layer_flush(ctx);
    debug_record_frame(ctx);
    const double rendered = GetTime();
    RenderImageAsset capture;
    ctx_ref<window_guard>(ctx).graphics->end_frame(ctx_ref<frame_state>(ctx).screenshots.empty() ? nullptr : &capture);
    save_pending_capture(ctx,capture);
    ctx_ref<frame_state>(ctx).cpu_render_ms = (f32)((rendered - updated) * 1000.0);
    ctx_ref<frame_state>(ctx).cpu_present_ms = (f32)((GetTime() - rendered) * 1000.0);
    if (low_power)
      pacer_sleep(ctx_ref<window_guard>(ctx).pacer, frame_start + 1.0 / ctx_ref<config>(ctx).target_fps - GetTime());
  }

  ecs_run(ctx, phase_shutdown);
}

void destroy(context *ctx) { delete ctx; }
} // namespace njin
