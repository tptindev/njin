#include "njin_cfg.h"
#include "njin_ctx_impl.h"
#include <raylib.h>

namespace njin {

f32 fps(const njin_ctx &ctx) { return ctx.cfg.target_fps; }
vec2 screen_size(const njin_ctx &) {
  return {(f32)GetScreenWidth(), (f32)GetScreenHeight()};
}

} // namespace njin
