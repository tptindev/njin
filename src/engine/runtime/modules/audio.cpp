#include "audio.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"

namespace njin {
namespace {
void update_audio(context &ctx) { audio_store_update(ctx.audio, ctx.time.dt_real); }

void setup(context &ctx) {
  ecs_register(ctx, phase_post_update, update_audio, "update_audio");
}
} // namespace

mod_desc audio_module() { return mod_desc{.name = "njin.audio", .setup = setup}; }
} // namespace njin
