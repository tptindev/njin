#include "njin.h"
#include "njin2rl.h"
#include "njin_ctx_impl.h"
#include <raylib.h>

namespace njin {
window_guard::window_guard(const njin_cfg &cfg) {
  InitWindow((i32)cfg.width, (i32)cfg.height, cfg.title);
  SetTargetFPS((i32)cfg.target_fps);
}

window_guard::~window_guard() { CloseWindow(); }

njin_ctx *njin_create(const njin_cfg &cfg) {
  njin_ctx *ctx = new njin_ctx(cfg);
  njin_mod_register(*ctx, core_module());
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
    ecs_run(ctx, phase_render);
    EndDrawing();
  }

  ecs_run(ctx, phase_shutdown);
}

void njin_destroy(njin_ctx *ctx) { delete ctx; }
} // namespace njin
