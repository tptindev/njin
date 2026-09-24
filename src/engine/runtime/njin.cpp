#include "njin.h"
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
  ctx.dt = GetFrameTime();
  ctx.elapsed = GetTime();

  njin_input &input = *ctx.input;
  while (!WindowShouldClose()) {
    input_key_poll(input);
    if (input_key_held(input, key_a)) {
      TraceLog(LOG_INFO, "HELLO");
    }
    BeginDrawing();
    ClearBackground(RAYWHITE);
    EndDrawing();
  }
}

void njin::njin_shutdown(njin_ctx &ctx) {
  delete ctx.input;
  ctx.input = nullptr;
  CloseWindow();
}
