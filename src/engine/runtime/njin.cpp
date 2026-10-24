#include "njin.h"
#include "modules/core_modules.h"
#include "modules/fx.h"
#include "modules/gizmo.h"
#include "njin2rl.h"
#include "njin_gpu_caps.h"
#include "njin_gpu_hint.h"
#include "njin_ctx_impl.h"
#include "njin_log_impl.h"
#include "njin_view.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <raylib.h>

namespace njin {
namespace {
// At most config::fixed_max_steps fixed steps per frame. After a long stall (a
// breakpoint, dragging the window) the backlog is dropped instead of being
// simulated in one frame, which would only make the next frame longer still.
void run_fixed_steps(context &ctx) {
  const i32 fixed_max_steps = std::max(ctx.cfg.fixed_max_steps, 1);
  time_state &time = ctx.time;
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
    if (ctx.hooks.fixed_step != nullptr)
      ctx.hooks.fixed_step(ctx, time.fixed_dt);
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
  InitWindow((i32)cfg.width, (i32)cfg.height, cfg.title);
  // Without a device (no speakers, driver problem) the game still runs; the
  // audio calls just fail to load and say so in the log. A game that plays no
  // sound opens none (config::audio).
  if (cfg.audio)
    InitAudioDevice();
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
  pacer_close(pacer);
  if (IsAudioDeviceReady())
    CloseAudioDevice();
  CloseWindow();
}

context *create(const config &cfg) {
  prefer_discrete_gpu();
  // Keep the log lines from here (the window's own GL and audio messages) for an
  // inspector that a game may attach before run; run lets go of them.
  log_hold(true);
  context *ctx = new context(cfg);
  ctx->time.fixed_dt = cfg.fixed_hz > 0.0f ? 1.0f / cfg.fixed_hz : 1.0f / 60.0f;
  ctx->random.reseed(
      (u64)std::chrono::high_resolution_clock::now().time_since_epoch().count());
  if (cfg.virtual_size.x >= 1.0f && cfg.virtual_size.y >= 1.0f)
    window_set_virtual_size(*ctx, cfg.virtual_size, cfg.integer_scale);
  ctx->view.render_scale =
      view_clamp_render_scale(cfg.render_scale, {(f32)GetScreenWidth(), (f32)GetScreenHeight()});
  if (ctx->view.render_scale != cfg.render_scale && cfg.render_scale > 1)
    NJIN_WARN("config::render_scale %d at the window's size would need a texture "
             "bigger than any GPU guarantees; using %dx instead",
             cfg.render_scale, ctx->view.render_scale);
  // The game's features (njin_features.h): their states, hooks and modules,
  // before the core modules register theirs among them.
  features_install(*ctx);
  register_core_modules(*ctx);
  return ctx;
}

// The version of the linked library. Built from the same macros as the header,
// so a game built against another header shows a different number.
const char *version() {
  static const std::string text = std::to_string(NJIN_VERSION_MAJOR) + "." + std::to_string(NJIN_VERSION_MINOR) +
                                  "." + std::to_string(NJIN_VERSION_PATCH);
  return text.c_str();
}

void run(context &ctx) {
  if (ctx.ecs.started) {
    return;
  }
  ctx.ecs.started = true;
  log_hold(false);
  NJIN_INFO("njin %s", version());
  // Driver shader compiles cost tens of milliseconds each: pay them here, not
  // on the first hit, pause or explosion. Not in low_power: there each is
  // loaded on its first use, and one never used costs no memory.
  if (!ctx.cfg.low_power) {
    fx_warmup(ctx);
    post_chain_warmup(ctx);
    particles_gpu_available(ctx);
  }
  ecs_run(ctx, phase_startup);

  Color clearbg = RAYWHITE;
  to_raylib(ctx.cfg.clear_bg_color, clearbg);
  const bool low_power = ctx.cfg.low_power;
  double frame_start = GetTime();
  while (!ctx.quit) {
    // raylib holds a close (the [x], Alt+F4, the exit key) for one frame.
    ctx.close_requested = WindowShouldClose();
    if (ctx.close_requested && !ctx.close_intercept)
      break;
    time_state &time = ctx.time;
    if (low_power) {
      // raylib runs uncapped here, so its frame time leaves out the sleep.
      const double now = GetTime();
      time.dt_real = (f32)(now - frame_start);
      frame_start = now;
    } else {
      time.dt_real = GetFrameTime();
    }
    time.elapsed = (f32)GetTime();
    time.dt = time.paused ? 0.0f : time.dt_real * time.scale;
    // Hitstop zeroes dt here, before any system (or fixed step) reads it.
    fx_frame_begin(ctx);
    gizmo_frame_begin(ctx);
    if (ctx.ecs.profile)
      ecs_profile_roll(ctx.ecs);
    view_frame_begin(ctx.view);
    input_key_poll(ctx.input);
    view_map_mouse(ctx.view, ctx.input.cur.mouse_pos, ctx.input.cur.mouse_delta);
    // A scene switch requested last frame happens here, before any system
    // of the new frame runs.
    scene_store_apply(ctx);

    ecs_run(ctx, phase_pre_update);
    run_fixed_steps(ctx);
    ecs_run(ctx, phase_update);
    ecs_run(ctx, phase_post_update);
    ctx.ecs.dispatcher.update();

    if (low_power && ctx.skip_draw) {
      // Nothing changed on screen: the window keeps its last image.
      ctx.skip_draw = false;
      PollInputEvents();
      pacer_sleep(ctx.window.pacer, frame_start + 1.0 / ctx.cfg.target_fps - GetTime());
      continue;
    }
    ctx.skip_draw = false;
    BeginDrawing();
    ClearBackground(clearbg);
    ctx.view.crisp_text = ctx.cfg.crisp_text && !gpu_is_software();
    ctx.view.smooth_ui = ctx.cfg.smooth_ui;
    view_draw_begin(ctx.view, clearbg);
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
    if (ctx.view.ui_window)
      view_ui_end(ctx.view);
    else
      view_draw_end(ctx.view);
    text_layer_flush(ctx);
    take_pending_screenshots(ctx);
    debug_record_frame(ctx);
    EndDrawing();
    if (low_power)
      pacer_sleep(ctx.window.pacer, frame_start + 1.0 / ctx.cfg.target_fps - GetTime());
  }

  ecs_run(ctx, phase_shutdown);
}

void destroy(context *ctx) { delete ctx; }
} // namespace njin
