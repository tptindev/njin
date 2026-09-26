#pragma once
#include "_comps.h"
#include "njin_particles.h"
#include <entt/entity/fwd.hpp>
#include <raylib.h>

namespace njin {
// GPU particles. An emitter with `gpu` set keeps only the spawn state of each
// particle on the CPU, with the emitter clock reading at which it was born in
// `particle::age`; a vertex shader works out where the particle is now, in
// closed form, and one instanced draw call paints the whole emitter. The
// emitter's particles live in a vertex buffer of its own that is written only
// when something is spawned or removed, so a frame costs no per-particle work
// on the CPU at all.
//
// The state is created on first use, when the GL context is known. It is
// declared in njin_ctx after the window, so its destructor frees the GL
// objects while the context is still alive.
struct particle_gpu_state {
  particle_backend backend = particle_backend_auto;
  // probe() ran; `available` is what it found. Both stay put afterwards.
  bool probed = false;
  bool available = false;

  Shader shader{};
  unsigned int quad_vbo = 0; // two triangles, the same for every particle

  int loc_mvp = -1;
  int loc_gravity = -1;
  int loc_drag = -1;
  int loc_clock = -1;
  int loc_sizes = -1;
  int loc_color_start = -1;
  int loc_color_end = -1;
  int loc_emitter_pos = -1;
  int loc_emitter_x = -1;
  int loc_emitter_y = -1;
  int loc_aspect = -1;
  int loc_mode = -1;
  int loc_source = -1;
  int loc_texture = -1;

  particle_gpu_state() = default;
  ~particle_gpu_state();
  // Copying would free the same GL objects twice.
  particle_gpu_state(const particle_gpu_state &) = delete;
  particle_gpu_state &operator=(const particle_gpu_state &) = delete;
};

// The GPU side of one emitter, kept as a component next to it. Owns the vertex
// array and the buffer of instances; the entity's destruction frees them, while
// the GL context is still alive (the ECS is destroyed first).
struct particle_gpu_buffer {
  // The emitter's own time: advanced by delta() while the emitter runs on the
  // GPU. A particle's age is `clock - particle::age`.
  f32 clock = 0.0f;
  // Next `clock` at which dead particles are removed. They cost a parked vertex
  // each until then, so this does not need to be often.
  f32 next_compact = 0.0f;
  unsigned int vao = 0;
  unsigned int vbo = 0;
  int capacity = 0;    // bytes in `vbo`
  usize uploaded = 0;  // particles at the front of the emitter's vector already in `vbo`

  particle_gpu_buffer() = default;
  ~particle_gpu_buffer();
  particle_gpu_buffer(particle_gpu_buffer &&other) noexcept;
  particle_gpu_buffer &operator=(particle_gpu_buffer &&other) noexcept;
  particle_gpu_buffer(const particle_gpu_buffer &) = delete;
  particle_gpu_buffer &operator=(const particle_gpu_buffer &) = delete;
};

// Advances the clock of a GPU emitter by `dt` and, now and then, removes its
// dead particles. Removing them (or shifting the clock back so it stays
// precise) makes the next draw upload the whole vector again.
void particles_gpu_step(particle_emitter &emitter, particle_gpu_buffer &buffer, f32 dt);

// How many particles of a GPU emitter are alive right now: the vector also
// holds the dead ones that have not been removed yet.
usize particles_gpu_alive(const particle_emitter &emitter, const particle_gpu_buffer &buffer);

// Whether an emitter with no live particles should run on the GPU: the backend
// allows it and this machine can. There is no size threshold: measured, one
// instanced draw call costs a few microseconds, and simulating and drawing one
// circle on the CPU about 4 (a textured quad far less), so even a 20-particle
// burst of circles is cheaper here.
bool particles_gpu_wanted(njin_ctx &ctx);

// Draws the particles of one emitter with a single instanced draw call.
void particles_gpu_draw(njin_ctx &ctx, entt::entity entity, const transform &tr,
                        const particle_emitter &emitter);
} // namespace njin
