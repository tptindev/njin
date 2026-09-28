#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_draw.h"
#include <entt/entity/fwd.hpp>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_particles
/// @{

/// A range of values. Each particle takes a random value in `[min, max]`.
/// `min == max` is a fixed value.
struct f32_range {
  f32 min = 0.0f; ///< Lower bound.
  f32 max = 0.0f; ///< Upper bound.
};

/// Shape of a particle when there is no texture.
enum particle_shape {
  particle_circle, ///< Circle, `size` is the diameter.
  particle_square, ///< Square, rotated by `spin`.
};

/// A live particle. Created and updated by the engine; the game only needs to read it if it wants to.
struct particle {
  vec2 pos{};        ///< Position (in the world, or relative to the emitter if `local_space`).
  vec2 velocity{};   ///< Velocity, units per second.
  f32 age = 0.0f;    ///< How long it has lived, in seconds.
  f32 life = 1.0f;   ///< Total lifetime, in seconds.
  f32 rot = 0.0f;    ///< Rotation angle, in degrees.
  f32 spin = 0.0f;   ///< Rotation speed, in degrees per second.
  f32 size = 1.0f;   ///< The particle's own size factor, multiplied into size_start/size_end.
};

/// Particle source: explosion, dust, sparks, smoke. Needs a transform on the same entity.
///
/// The engine's particle module spawns and updates particles in `phase_post_update`
/// using delta() (so it stops on pause and slows down with time_set_scale()), and
/// draws them in `phase_render` together with sprites, by `layer`. On the same
/// layer, particles are drawn after sprites.
///
/// Two ways to emit:
/// - **Continuous**: `rate > 0` and `emitting = true`, for example smoke from a chimney.
/// - **A batch**: particles_burst(), for example an explosion. Use
///   particles_spawn() to create an entity that only bursts once and then
///   destroys itself.
///
/// Every field can be changed while running; particles already spawned keep
/// their speed and lifetime, while color and size always follow the current values.
///
/// On a machine with a GPU, particles are simulated in the vertex shader and
/// drawn with one instanced call per emitter (see particle_backend). In that case
/// `gpu` is true and only `age` is updated in `particles`: `pos`, `velocity`,
/// `rot` keep their values from spawn time, because the graphics card computes
/// the current position from them itself. Check `gpu` first before reading particle positions.
struct particle_emitter {
  /// @name Emission
  /// @{
  f32 rate = 0.0f;       ///< Particles per second when emitting continuously. 0 means bursts only.
  bool emitting = true;  ///< Emitting continuously. Does not affect particles_burst().
  i32 max_particles = 512; ///< Maximum number of live particles. New particles are dropped when full.
  /// Spawn area around the entity's position: a rectangle centered at
  /// `transform.pos`. `{0, 0}` spawns at exactly one point.
  vec2 area{};
  /// @}

  /// @name Motion
  /// @{
  f32_range life{0.5f, 1.0f};     ///< Lifetime, in seconds.
  f32_range speed{50.0f, 100.0f}; ///< Initial speed.
  /// Emission direction, in degrees (0 is right, 90 is down), added to the
  /// transform's rotation angle.
  f32 angle = 0.0f;
  /// Spread around `angle`, in degrees. 360 is every direction.
  f32 spread = 360.0f;
  vec2 gravity{};     ///< Acceleration, units per second squared. `{0, 400}` falls downward.
  f32 drag = 0.0f;    ///< Drag: velocity decreases by this ratio each second. 0 is no drag.
  f32_range spin{};   ///< Rotation speed, in degrees per second.
  /// Particles follow the emitter when the emitter moves (for example fire at a
  /// rocket's tail). `false` means particles leave a trail in the world.
  bool local_space = false;
  /// @}

  /// @name Appearance
  /// @{
  f32 size_start = 8.0f; ///< Size at spawn, in world pixels.
  f32 size_end = 0.0f;   ///< Size at death.
  f32_range size_jitter{1.0f, 1.0f}; ///< Random size factor for each individual particle.
  rgba color_start{1.0f, 1.0f, 1.0f, 1.0f}; ///< Color at spawn.
  rgba color_end{1.0f, 1.0f, 1.0f, 0.0f};   ///< Color at death. Fades out by default.
  particle_shape shape = particle_circle; ///< Shape when there is no texture.
  /// Particle image. If invalid, `shape` is drawn. The image is scaled so its
  /// longest side equals the particle size, then multiplied by the particle's color.
  texture_handle texture{};
  rect source{};  ///< Region in the image, in pixels. Size 0 is the whole image.
  blend_mode blend = blend_alpha; ///< Color blending mode. `blend_additive` for fire, sparks.
  i32 layer = 0;  ///< Draw layer, same convention as sprite::layer.
  bool visible = true; ///< Hides without stopping the simulation.
  /// @}

  /// Destroys the entity when it is no longer emitting (`emitting == false` or
  /// `rate == 0`), no burst is waiting, and all particles have died. Use for one-shot effects.
  bool destroy_when_done = false;

  /// @name State (managed by the engine)
  /// @{
  std::vector<particle> particles; ///< Live particles. See the note about `gpu` above.
  i32 pending_burst = 0; ///< Number of particles waiting to spawn on the next update, see particles_burst().
  f32 emit_accum = 0.0f; ///< Fractional part of a particle not yet spawned during continuous emission.
  /// Being simulated and drawn on the GPU. The engine chooses when the emitter has no particles, see particle_backend.
  bool gpu = false;
  /// @}
};

/// Where particles are simulated and drawn.
enum particle_backend {
  /// On a machine with a GPU (OpenGL 3.3 or later, not a software renderer)
  /// every emitter runs on the GPU; on a machine without one, on the CPU.
  particle_backend_auto,
  particle_backend_cpu, ///< Every emitter runs on the CPU, even if the machine has a GPU.
};

/// Chooses where particles are simulated. The default is particle_backend_auto.
///
/// Each emitter only changes where it runs when it has no particles left, so
/// switching midway does not make particles in flight stutter.
/// @param ctx Engine context.
/// @param backend The choice.
void particles_set_backend(njin_ctx &ctx, particle_backend backend);

/// The choice set with particles_set_backend().
/// @param ctx Engine context.
/// @return The current choice.
particle_backend particles_backend(const njin_ctx &ctx);

/// Whether this machine can run particles on the GPU: OpenGL 3.3 or later, with
/// instancing, and not a software renderer (llvmpipe, SwiftShader, Microsoft
/// Basic Render Driver, ...). Only correct after the window has opened, that is,
/// inside any game function.
/// @param ctx Engine context.
/// @return `true` if the GPU is usable.
bool particles_gpu_available(njin_ctx &ctx);

/// Queues `count` particles to spawn together on the next update.
///
/// Does not need `emitting`. Calling it several times in one frame accumulates.
/// @param emitter The emitter.
/// @param count Number of particles.
inline void particles_burst(particle_emitter &emitter, i32 count) {
  if (count > 0)
    emitter.pending_burst += count;
}

/// Creates an entity at `pos` only to emit one burst of `count` particles,
/// following the `preset` template, then destroys itself when the last particle dies.
///
/// `preset` is usually a game constant, for example `explosion`. The new entity
/// is attached to the running scene (njin::scene_owned), so it disappears when
/// the scene is left.
/// @param ctx Engine context.
/// @param preset Emitter template. `emitting` and `destroy_when_done` are overwritten.
/// @param pos Position in the world.
/// @param count Number of particles.
/// @return The entity just created.
entt::entity particles_spawn(njin_ctx &ctx, const particle_emitter &preset,
                             vec2 pos, i32 count);
/// @}
} // namespace njin
