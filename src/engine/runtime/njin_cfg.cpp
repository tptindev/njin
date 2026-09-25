#include "njin_cfg.h"
#include "njin_ctx_impl.h"

namespace njin {

f32 fps(const njin_ctx &ctx) { return ctx.cfg.target_fps; }
vec2 screen_size(const njin_ctx &ctx) {
  return {ctx.cfg.width, ctx.cfg.height};
}

} // namespace njin
