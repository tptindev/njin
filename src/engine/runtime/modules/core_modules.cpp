#include "core_modules.h"
#include "audio.h"
#include "anim.h"
#include "camera.h"
#include "collision.h"
#include "debug.h"
#include "hierarchy.h"
#include "particles.h"
#include "reload.h"
#include "ui.h"
#include "njin_ctx.h"
#include "sprite.h"

namespace njin {
// Order matters inside a phase: reload first, so a changed file is swapped
// in before anything of the frame uses it; debug next, so the inspector's
// pause and step apply before any system reads the clock; ui next, so it takes its keys
// before any game system reads them; then camera, so its pre_render opens the
// world pass before anything draws; hierarchy before anim, particles and
// sprite, so they all see this frame's child transforms; and sprite before
// any game module, so game render systems draw on top of sprites, tilemaps
// and particles. Collision comes after sprite: its detection only needs the
// hierarchy to have run, and its debug outlines then draw over the sprites.
void register_core_modules(njin_ctx &ctx) {
  njin_mod_register(ctx, {reload_module(), debug_module(), ui_module(), camera_module(), audio_module(), hierarchy_module(),
                          anim_module(), particles_module(), sprite_module(),
                          collision_module()});
}
} // namespace njin
