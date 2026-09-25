#pragma once
#include "_comps.h"
#include "_mod.h"
#include "njin_particles.h"

namespace njin {
// Core module. Spawns, moves and ages the particles of every
// particle_emitter in phase_post_update, and destroys finished one-shot
// emitters. Drawing is done by the sprite module, which calls
// particles_draw() so emitters sort by layer together with sprites.
mod_desc particles_module();

// Draws the particles of one emitter in world space, with its blend mode.
void particles_draw(const njin_ctx &ctx, const transform &tr,
                    const particle_emitter &emitter);
} // namespace njin
