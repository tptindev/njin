#include "njin_cfg.h"
#include "njin_ctx_impl.h"
#include <raylib.h>

namespace njin {

f32 fps(const context &ctx) { return ctx.cfg.target_fps; }
// screen_size lives in njin_view.cpp: it answers the virtual size when one is set.

} // namespace njin
