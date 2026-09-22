#include <njin.h>
#include <raylib.h>

void njin::njin_init(njin_ctx &ctx, const njin_cfg &cfg) {
  ctx.cfg = cfg;

  InitWindow((i32)ctx.cfg.width, (i32)ctx.cfg.height, ctx.cfg.title);
  SetTargetFPS((i32)ctx.cfg.target_fps);
}

void njin::njin_run(njin_ctx &ctx) {
  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    EndDrawing();
  }
}

void njin::njin_shutdown(njin_ctx &ctx) { CloseWindow(); }
