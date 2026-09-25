#include "core_modules.h"
#include "audio.h"
#include "anim.h"
#include "camera.h"
#include "collision.h"
#include "hierarchy.h"
#include "particles.h"
#include "njin_ctx.h"
#include "sprite.h"

namespace njin {
// Order matters inside a phase: camera first, so its pre_render opens the
// world pass before anything draws; hierarchy before anim, particles and
// sprite, so they all see this frame's child transforms; and sprite before
// any game module, so game render systems draw on top of sprites, tilemaps
// and particles. Collision comes after sprite: its detection only needs the
// hierarchy to have run, and its debug outlines then draw over the sprites.
void register_core_modules(njin_ctx &ctx) {
  njin_mod_register(ctx, {camera_module(), audio_module(), hierarchy_module(),
                          anim_module(), particles_module(), sprite_module(),
                          collision_module()});
}
} // namespace njin
