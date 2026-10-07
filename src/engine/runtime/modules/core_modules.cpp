#include "core_modules.h"
#include "audio.h"
#include "body.h"
#include "follow.h"
#include "anim.h"
#include "camera.h"
#include "collision.h"
#include "debug.h"
#include "dialog.h"
#include "hierarchy.h"
#include "particles.h"
#include "particles3d.h"
#include "render3d.h"
#include "reload.h"
#include "ui.h"
#include "njin_ctx.h"
#include "njin_script_impl.h"
#include "njin_world3d_impl.h"
#include "sprite.h"
#include "timer.h"

namespace njin {
// Order matters inside a phase: reload first, so a changed file is swapped
// in before anything of the frame uses it; debug next, so the inspector's
// pause and step apply before any system reads the clock; ui next, so it takes its keys
// before any game system reads them; then camera, so its pre_render opens the
// world pass before anything draws; hierarchy before anim, particles and
// sprite, so they all see this frame's child transforms; and sprite before
// any game module, so game render systems draw on top of sprites, tilemaps
// and particles. Camera follow sits between hierarchy and sprite: it reads
// this frame's positions, and the sprite module prepares tilemap chunks for
// the view it settles on. Collision comes after sprite: its detection only
// needs the hierarchy to have run, and its debug outlines then draw over the
// sprites. Body moves characters in fixed_update, before any game module's
// fixed systems. Script comes last: entity scripts update after the timers
// and draw over sprites, before any game module's systems.
void register_core_modules(context &ctx) {
  mod_register(ctx, {reload_module(), debug_module(), ui_module(), dialog_module(), camera_module(),
                          audio_module(), hierarchy_module(), camera_follow_module(),
                          anim_module(), particles_module(), particles3d_module(), render3d_module(), world3d_module(), sprite_module(),
                          collision_module(), body_module(), timer_module(), script_module()});
}
} // namespace njin
