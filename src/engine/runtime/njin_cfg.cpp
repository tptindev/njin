#include "njin_cfg.h"
#include "njin_ctx_impl.h"

void njin::get_fps(const njin_ctx &ctx, f32 &fps) { fps = ctx.cfg.target_fps; }
void njin::get_screen_size(const njin_ctx &ctx, vec2 &screen_size) {
  screen_size.x = ctx.cfg.width;
  screen_size.y = ctx.cfg.height;
}
