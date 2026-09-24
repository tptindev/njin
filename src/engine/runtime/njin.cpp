#include "njin.h"
#include "njin2rl.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_input.h"
#include <raylib.h>

void njin::njin_init(njin_ctx &ctx, const njin_cfg &cfg) {
  ctx.cfg = cfg;
  ctx.input = new njin_input();
  InitWindow((i32)ctx.cfg.width, (i32)ctx.cfg.height, ctx.cfg.title);
  SetTargetFPS((i32)ctx.cfg.target_fps);
}

void njin::njin_run(njin_ctx &ctx) {
  Color clearbg = RAYWHITE;
  to_raylib(ctx.cfg.clear_bg_color, clearbg);
  while (!WindowShouldClose()) {
    ctx.dt = GetFrameTime();
    ctx.elapsed = (f32)GetTime();
    input_key_poll(*ctx.input);

    BeginDrawing();
    ClearBackground(clearbg);
    EndDrawing();
  }
}

void njin::njin_shutdown(njin_ctx &ctx) {
  delete ctx.input;
  ctx.input = nullptr;
  CloseWindow();
}
