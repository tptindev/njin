#include "audio.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_audio_impl.h"

namespace njin {
namespace {
void update_audio(context &ctx) {
  if (ctx_ref<feature_hooks>(ctx).audio_update != nullptr)
    ctx_ref<feature_hooks>(ctx).audio_update(ctx);
  audio_store_update(ctx_ref<audio_store>(ctx), ctx_ref<time_state>(ctx).dt_real);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_post_update, update_audio, "update_audio");
}
} // namespace

mod_desc audio_module() { return mod_desc{.name = "njin.audio", .setup = setup}; }
} // namespace njin
