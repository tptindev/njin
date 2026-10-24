#include "njin_cfg.h"
#include "njin_ctx_impl.h"
#include <raylib.h>

namespace njin {

f32 fps(const context &ctx) { return ctx.cfg.target_fps; }

void set_fps(context &ctx, f32 target) {
  ctx.cfg.target_fps = target < 1.0f ? 1.0f : target;
  if (!ctx.cfg.low_power) // low_power: run() paces the frames itself
    SetTargetFPS((int)ctx.cfg.target_fps);
}

void frame_skip_draw(context &ctx) { ctx.skip_draw = true; }
// screen_size lives in njin_view.cpp: it answers the virtual size when one is set.

} // namespace njin
