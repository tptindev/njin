#pragma once
#include "../njin_internal_only.h"
#include "_comps.h"
#include "_mod.h"
#include "njin_particles.h"

namespace njin {
// Core module. Spawns, moves and ages the particles of every
// particle_emitter in phase_post_update, and destroys finished one-shot
// emitters. Drawing is done by the sprite module, which calls
// particles_draw() so emitters sort by layer together with sprites.
mod_desc particles_module();

// Where the live particles of an emitter can be, kept as a component next to it
// by the particle module so the draw pass can skip an emitter that is off
// screen without looking at its particles (the GPU path never reads them).
// Reset whenever the emitter has none.
struct particle_extent {
  bool any = false;
  vec2 lo{}, hi{};   // box of the spawn points (local to the emitter if local_space)
  f32 reach = 0.0f;  // how far a particle can travel from its spawn point
  f32 life = 0.0f;   // longest life spawned since the reset
  f32 size = 0.0f;   // biggest size factor spawned since the reset
};

// False when every particle of `emitter` is certainly outside `view`.
bool particles_on_screen(const transform &tr, const particle_emitter &emitter,
                         const particle_extent &extent, const rect &view);

// Draws the particles of one emitter in world space, with its blend mode, on
// the CPU or the GPU depending on where the emitter runs (particle_emitter::gpu,
// see particles_gpu.h).
void particles_draw(njin_ctx &ctx, entt::entity entity, const transform &tr,
                    const particle_emitter &emitter);
} // namespace njin
