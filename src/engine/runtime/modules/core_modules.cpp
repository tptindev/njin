#include "core_modules.h"
#include "audio.h"
#include "camera.h"
#include "njin_ctx.h"

namespace njin {
void register_core_modules(njin_ctx &ctx) {
  njin_mod_register(ctx, camera_module());
  njin_mod_register(ctx, audio_module());
}
} // namespace njin
