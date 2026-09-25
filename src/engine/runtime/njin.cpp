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

njin_ctx *njin_create(const njin_cfg &cfg) { return new njin_ctx(cfg); }

void njin_run(njin_ctx &ctx) {
  Color clearbg = RAYWHITE;
  to_raylib(ctx.cfg.clear_bg_color, clearbg);
  while (!WindowShouldClose()) {
    ctx.dt = GetFrameTime();
    ctx.elapsed = (f32)GetTime();
    input_key_poll(ctx.input);

    BeginDrawing();
    ClearBackground(clearbg);
    EndDrawing();
  }
}

void njin_destroy(njin_ctx *ctx) { delete ctx; }
} // namespace njin
