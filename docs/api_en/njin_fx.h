#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_particles.h"
#include <entt/entity/fwd.hpp>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_fx
/// @{

/// @name Screen and time effects
/// @{

/// How camera_shake() shakes. Set once with camera_shake_config(), or
/// leave the defaults.
struct shake_config {
  f32 max_offset = 16.0f; ///< Maximum offset when shake = 1, in screen pixels.
  f32 max_angle = 3.0f;   ///< Maximum angle offset when shake = 1, in degrees.
  f32 frequency = 30.0f;  ///< Shake speed, times per second.
  f32 decay = 1.5f;       ///< How much the shake decreases per second.
};

/// Shakes the camera. Accumulates: consecutive hits shake harder and harder.
///
/// The shake amount (0..1) decreases over time by `shake_config::decay`. The real intensity is proportional to
/// the square of the shake amount, so small shakes are barely visible and large shakes are very strong:
/// 0.2 for heavy footsteps, 0.4 for taking a hit, 0.7 or more for explosions. It only
/// affects what is drawn, and changes neither the camera's transform nor w2scr()/scr2w().
/// It uses real time, so it still shakes during hitstop().
/// @param ctx Engine context.
/// @param trauma Extra shake amount, with the total capped at 1.
void camera_shake(njin_ctx &ctx, f32 trauma);

/// Changes how camera_shake() shakes.
/// @param ctx Engine context.
/// @param config New shake settings.
void camera_shake_config(njin_ctx &ctx, const shake_config &config);

/// Current shake amount, 0..1.
/// @param ctx Engine context.
/// @return Shake amount.
f32 camera_shake_amount(const njin_ctx &ctx);

/// Freezes the game briefly (hitstop, freeze frame) to give a hit "weight".
///
/// While frozen, delta() returns 0 and `phase_fixed_update` does not run, like
/// time_set_paused(), but it ends by itself after `seconds` real seconds. Calling it while already frozen
/// takes the longer time, it does not accumulate. Usually 0.03 to 0.12 seconds.
/// @param ctx Engine context.
/// @param seconds Freeze duration, in real seconds.
void hitstop(njin_ctx &ctx, f32 seconds);

/// Whether a hitstop is active.
/// @param ctx Engine context.
/// @return `true` if the game is frozen.
bool hitstop_active(const njin_ctx &ctx);

/// Flashes the whole screen in one color and then fades it out: white for a big explosion, red when hurt.
///
/// Drawn over everything, including the UI (but under the scene transition effect). Uses real
/// time. Calling it again while flashing replaces it with the new flash.
/// @param ctx Engine context.
/// @param color Flash color. `color.a` is the initial opacity.
/// @param duration Time to fade out completely, in seconds.
void screen_flash(njin_ctx &ctx, rgba color, f32 duration);
/// @}

/// @name Sprite flash
/// @{

/// Paints a sprite one color for a moment, usually white when hit.
///
/// Unlike `sprite.tint` (which only multiplies color and cannot brighten), this flash replaces the
/// color of every pixel while keeping the sprite's shape. The sprite module removes the
/// component itself when time runs out. Use sprite_flash() for convenience.
struct flash_fx {
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Paint color. `a` is the initial coverage.
  f32 duration = 0.1f; ///< Flash duration, in seconds.
  f32 time = 0.0f;     ///< Elapsed time, updated by the engine.
  bool fade = true;    ///< Fades out. `false` keeps the paint color until time runs out.
};

/// Flashes the sprite of `entity`. Calling it again while flashing restarts it.
/// @param ctx Engine context.
/// @param entity Entity with a sprite.
/// @param color Paint color.
/// @param duration Duration, in seconds (follows delta(), so it pauses during hitstop).
void sprite_flash(njin_ctx &ctx, entt::entity entity,
                  rgba color = {1.0f, 1.0f, 1.0f, 1.0f}, f32 duration = 0.1f);
/// @}

/// @name Sprite dissolve
/// @{

/// Makes a sprite dissolve away in small patches, with a glowing burnt edge where it is dissolving:
/// a dead enemy, a vanishing item. Set `reverse` for the opposite, the sprite appears gradually.
///
/// Each patch has a fixed random number (from a hash function, no noise image needed); a patch whose number is below
/// the running threshold going from 0 to 1 disappears. The sprite's shape is kept, only patches are removed.
/// The sprite module updates `time` by delta() (so it pauses during hitstop).
///
/// When time runs out:
/// - dissolve (default): the sprite is **fully hidden** and the component stays, so it does not reappear. Set
///   `destroy_when_done` to destroy the entity as well, or remove the component yourself to make the sprite reappear;
/// - `reverse`: the component is removed automatically and the sprite is fully visible.
///
/// Can be combined with njin::flash_fx: flash white and then dissolve.
/// Use sprite_dissolve() for convenience.
struct dissolve_fx {
  rgba edge_color{1.0f, 0.55f, 0.1f, 1.0f}; ///< Burnt edge color. `a` is the strength, 0 removes the edge.
  f32 edge_width = 0.08f; ///< Edge thickness, on the 0..1 random scale. 0 removes the edge.
  f32 grain = 2.0f;       ///< Size of each patch, in pixels of the sprite image. 1 is single pixels, larger is bigger patches.
  f32 seed = 0.0f;        ///< Changes the dissolve pattern. Give each enemy its own value so they do not dissolve identically.
  f32 duration = 0.6f;    ///< Time to dissolve completely, in seconds.
  f32 time = 0.0f;        ///< Elapsed time, updated by the engine.
  bool reverse = false;   ///< `true`: appears gradually instead of dissolving.
  bool destroy_when_done = false; ///< Destroys the entity when fully dissolved. Does not apply to `reverse`.
};

/// Dissolves the sprite of `entity`. Calling it again while dissolving restarts it.
///
/// To tweak more (appear gradually, patch size, destroy when done) attach a njin::dissolve_fx yourself:
/// @code
/// reg.emplace_or_replace<njin::dissolve_fx>(enemy, njin::dissolve_fx{.duration = 0.8f, .seed = 3.0f,
///                                                                     .destroy_when_done = true});
/// @endcode
/// @param ctx Engine context.
/// @param entity Entity with a sprite.
/// @param duration Duration, in seconds (follows delta(), so it pauses during hitstop).
/// @param edge_color Burnt edge color. `a` equal to 0 means no edge.
void sprite_dissolve(njin_ctx &ctx, entt::entity entity, f32 duration = 0.6f,
                     rgba edge_color = {1.0f, 0.55f, 0.1f, 1.0f});
/// @}

/// Commonly used particle presets, to pass to particles_spawn() or attach directly
/// to an entity. They are plain values: edit them freely before use.
///
/// @code
/// njin::particles_spawn(ctx, njin::fx::explosion(), pos, 60);
///
/// njin::particle_emitter smoke = njin::fx::smoke();
/// smoke.color_start = {0.3f, 0.3f, 0.35f, 0.6f};
/// reg.emplace<njin::particle_emitter>(chimney, smoke); // emits continuously
/// @endcode
namespace fx {
/// Explosion: spreads in every direction, fast, yellow-orange turning dark red, bright (additive).
/// Burst it with particles_spawn() using 40–80 particles.
/// @return The configured emitter.
inline particle_emitter explosion() {
  particle_emitter e{};
  e.life = {0.35f, 0.8f};
  e.speed = {120.0f, 420.0f};
  e.drag = 4.0f;
  e.size_start = 18.0f;
  e.size_end = 2.0f;
  e.size_jitter = {0.6f, 1.4f};
  e.color_start = {1.0f, 0.85f, 0.35f, 1.0f};
  e.color_end = {0.7f, 0.1f, 0.05f, 0.0f};
  e.blend = blend_additive;
  e.area = {8.0f, 8.0f};
  return e;
}

/// Sparks: small, very fast, falling under gravity. For metal impacts, bullets
/// hitting walls. Burst 10–25 particles; adjust `angle`/`spread` to shoot to one side.
/// @return The configured emitter.
inline particle_emitter sparks() {
  particle_emitter e{};
  e.life = {0.2f, 0.5f};
  e.speed = {200.0f, 500.0f};
  e.gravity = {0.0f, 900.0f};
  e.drag = 1.5f;
  e.size_start = 4.0f;
  e.size_end = 1.0f;
  e.color_start = {1.0f, 0.95f, 0.6f, 1.0f};
  e.color_end = {1.0f, 0.45f, 0.1f, 0.0f};
  e.shape = particle_square;
  e.blend = blend_additive;
  return e;
}

/// Dust: rises slowly, gray-brown, fading out. For running steps, landing, sliding. Burst
/// 6–12 particles at the feet.
/// @return The configured emitter.
inline particle_emitter dust() {
  particle_emitter e{};
  e.life = {0.3f, 0.6f};
  e.speed = {20.0f, 70.0f};
  e.angle = -90.0f;
  e.spread = 140.0f;
  e.gravity = {0.0f, -20.0f};
  e.drag = 3.0f;
  e.size_start = 6.0f;
  e.size_end = 12.0f;
  e.size_jitter = {0.7f, 1.3f};
  e.color_start = {0.75f, 0.7f, 0.62f, 0.7f};
  e.color_end = {0.75f, 0.7f, 0.62f, 0.0f};
  e.area = {16.0f, 2.0f};
  return e;
}

/// Smoke: rises, expands, fading out. Emits continuously at about 15–30 particles per second.
/// @return The configured emitter.
inline particle_emitter smoke() {
  particle_emitter e{};
  e.rate = 20.0f;
  e.life = {1.2f, 2.2f};
  e.speed = {20.0f, 45.0f};
  e.angle = -90.0f;
  e.spread = 30.0f;
  e.drag = 0.5f;
  e.spin = {-40.0f, 40.0f};
  e.size_start = 10.0f;
  e.size_end = 36.0f;
  e.color_start = {0.55f, 0.55f, 0.58f, 0.5f};
  e.color_end = {0.35f, 0.35f, 0.38f, 0.0f};
  e.area = {6.0f, 2.0f};
  return e;
}

/// Fire: a continuously emitting flame, rising, yellow turning red, bright (additive).
/// @return The configured emitter.
inline particle_emitter fire() {
  particle_emitter e{};
  e.rate = 70.0f;
  e.life = {0.4f, 0.8f};
  e.speed = {50.0f, 120.0f};
  e.angle = -90.0f;
  e.spread = 25.0f;
  e.size_start = 22.0f;
  e.size_end = 2.0f;
  e.size_jitter = {0.7f, 1.2f};
  e.color_start = {1.0f, 0.8f, 0.3f, 0.9f};
  e.color_end = {0.9f, 0.15f, 0.05f, 0.0f};
  e.blend = blend_additive;
  e.area = {14.0f, 4.0f};
  return e;
}

/// Sparkle: small spinning particles, drifting gently, bright. For picking up items, magic, healing.
/// Burst 12–20 particles, or emit continuously around an item.
/// @return The configured emitter.
inline particle_emitter sparkle() {
  particle_emitter e{};
  e.life = {0.4f, 0.9f};
  e.speed = {30.0f, 120.0f};
  e.drag = 2.5f;
  e.gravity = {0.0f, -30.0f};
  e.spin = {-360.0f, 360.0f};
  e.size_start = 7.0f;
  e.size_end = 0.0f;
  e.color_start = {1.0f, 1.0f, 0.75f, 1.0f};
  e.color_end = {0.5f, 0.8f, 1.0f, 0.0f};
  e.shape = particle_square;
  e.blend = blend_additive;
  e.area = {12.0f, 12.0f};
  return e;
}

/// Debris: square chunks thrown up and then falling, spinning. For breaking crates, breaking rocks. Burst 8–16
/// particles; change `color_start`/`color_end` to match the material.
/// @return The configured emitter.
inline particle_emitter debris() {
  particle_emitter e{};
  e.life = {0.6f, 1.1f};
  e.speed = {120.0f, 320.0f};
  e.angle = -90.0f;
  e.spread = 120.0f;
  e.gravity = {0.0f, 1100.0f};
  e.spin = {-540.0f, 540.0f};
  e.size_start = 7.0f;
  e.size_end = 5.0f;
  e.size_jitter = {0.6f, 1.4f};
  e.color_start = {0.55f, 0.4f, 0.25f, 1.0f};
  e.color_end = {0.45f, 0.32f, 0.2f, 0.0f};
  e.shape = particle_square;
  return e;
}

/// Blood or liquid splash: red droplets thrown out and then falling. Burst 10–20 particles, with
/// `angle` set along the direction of the hit.
/// @return The configured emitter.
inline particle_emitter splash() {
  particle_emitter e{};
  e.life = {0.35f, 0.7f};
  e.speed = {100.0f, 300.0f};
  e.spread = 70.0f;
  e.gravity = {0.0f, 900.0f};
  e.size_start = 6.0f;
  e.size_end = 2.0f;
  e.size_jitter = {0.6f, 1.3f};
  e.color_start = {0.75f, 0.05f, 0.08f, 1.0f};
  e.color_end = {0.45f, 0.0f, 0.02f, 0.0f};
  return e;
}

/// Rain: fast falling streaks, covering a wide area. Attach it to an entity that follows the camera,
/// and set `area` to the screen width (and a bit larger).
/// @return The configured emitter.
inline particle_emitter rain() {
  particle_emitter e{};
  e.rate = 250.0f;
  e.max_particles = 1500;
  e.life = {0.6f, 0.9f};
  e.speed = {700.0f, 900.0f};
  e.angle = 100.0f;
  e.spread = 4.0f;
  e.size_start = 3.0f;
  e.size_end = 3.0f;
  e.color_start = {0.7f, 0.8f, 1.0f, 0.55f};
  e.color_end = {0.7f, 0.8f, 1.0f, 0.3f};
  e.shape = particle_square;
  e.area = {1400.0f, 10.0f};
  return e;
}

/// Snow: white flakes falling slowly, swaying gently. Use it like rain().
/// @return The configured emitter.
inline particle_emitter snow() {
  particle_emitter e{};
  e.rate = 60.0f;
  e.max_particles = 1500;
  e.life = {6.0f, 9.0f};
  e.speed = {30.0f, 70.0f};
  e.angle = 90.0f;
  e.spread = 50.0f;
  e.size_start = 4.0f;
  e.size_end = 4.0f;
  e.size_jitter = {0.5f, 1.3f};
  e.color_start = {1.0f, 1.0f, 1.0f, 0.9f};
  e.color_end = {1.0f, 1.0f, 1.0f, 0.0f};
  e.area = {1400.0f, 10.0f};
  return e;
}

/// Trail: particles left behind as an entity moves (rocket, dart,
/// shooting star). Emits continuously, does not fly, only shrinks and fades out.
/// @return The configured emitter.
inline particle_emitter trail() {
  particle_emitter e{};
  e.rate = 80.0f;
  e.life = {0.25f, 0.4f};
  e.speed = {0.0f, 8.0f};
  e.size_start = 8.0f;
  e.size_end = 0.0f;
  e.color_start = {1.0f, 1.0f, 1.0f, 0.8f};
  e.color_end = {0.6f, 0.8f, 1.0f, 0.0f};
  e.blend = blend_additive;
  return e;
}
} // namespace fx
/// @}
} // namespace njin
