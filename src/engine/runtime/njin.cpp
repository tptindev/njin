#include "njin.h"
#include "njin2rl.h"
#include "njin_ctx_impl.h"
#include "njin_log_impl.h"
#include "modules/core_modules.h"
#include <raylib.h>

namespace njin {
window_guard::window_guard(const njin_cfg &cfg) {
  log_capture_raylib();
  InitWindow((i32)cfg.width, (i32)cfg.height, cfg.title);
  // Without a device (no speakers, driver problem) the game still runs; the
  // audio calls just fail to load and say so in the log.
  InitAudioDevice();
  SetTargetFPS((i32)cfg.target_fps);
}

window_guard::~window_guard() {
  if (IsAudioDeviceReady())
    CloseAudioDevice();
  CloseWindow();
}

njin_ctx *njin_create(const njin_cfg &cfg) {
  njin_ctx *ctx = new njin_ctx(cfg);
  register_core_modules(*ctx);
  return ctx;
}

void njin_run(njin_ctx &ctx) {
  if (ctx.ecs.started) {
    return;
  }
  ctx.ecs.started = true;
  ecs_run(ctx, phase_startup);

  Color clearbg = RAYWHITE;
  to_raylib(ctx.cfg.clear_bg_color, clearbg);
  while (!WindowShouldClose()) {
    ctx.dt = GetFrameTime();
    ctx.elapsed = (f32)GetTime();
    input_key_poll(ctx.input);

    ecs_run(ctx, phase_pre_update);
    ecs_run(ctx, phase_update);
    ecs_run(ctx, phase_post_update);
    ctx.ecs.dispatcher.update();

    BeginDrawing();
    ClearBackground(clearbg);
    ecs_run(ctx, phase_pre_render);
    ecs_run(ctx, phase_render);
    ecs_run(ctx, phase_post_render);
    EndDrawing();
  }

  ecs_run(ctx, phase_shutdown);
}

void njin_destroy(njin_ctx *ctx) { delete ctx; }
} // namespace njin
