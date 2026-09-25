#include "core_modules.h"
#include "audio.h"
#include "camera.h"
#include "njin_ctx.h"
#include "sprite.h"

namespace njin {
// Order matters inside a phase: camera first, so its pre_render opens the
// world pass before anything draws, and sprite before any game module, so
// game render systems draw on top of sprites and tilemaps.
void register_core_modules(njin_ctx &ctx) {
  njin_mod_register(ctx, {camera_module(), audio_module(), sprite_module()});
}
} // namespace njin
