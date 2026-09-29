#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"
#include "njin_3d.h"
#include <raylib.h>
#include <vector>

namespace njin {
struct context;

// 3D particles (particles3d_spawn in njin_3d.h). Core module: moves and ages
// every burst in phase_post_update, like the 2D particle module. Drawn by
// render3d at end_3d, as camera-facing quads, inside that pass.
//
// A burst keeps a copy of the emitter it came from (for colours, sizes,
// gravity, drag, blend, texture) and its own particles; it goes away with its
// last particle.
mod_desc particles3d_module();

struct particle3d {
  vec3 pos{};
  vec3 velocity{};
  f32 life = 1.0f; // seconds this one lives
  f32 age = 0.0f;
  f32 size = 1.0f; // size_jitter factor
  f32 rotation = 0.0f;
  f32 spin = 0.0f; // degrees per second
};

struct particle3d_burst {
  particle_emitter look; // the emitter, particles vector left empty
  f32 scale = 1.0f;      // emitter units to world units
  std::vector<particle3d> particles;
};

// Generated once on first draw: a white disc for particle_circle.
struct particles3d_state {
  std::vector<particle3d_burst> bursts;
  Texture2D disc{};

  particles3d_state() = default;
  ~particles3d_state();
  particles3d_state(const particles3d_state &) = delete;
  particles3d_state &operator=(const particles3d_state &) = delete;
};

// Draws every live particle facing `camera`. Called by end_3d while its pass
// (projection, view, depth test) is still set.
void particles3d_draw(context &ctx, const camera3d &camera);
} // namespace njin
