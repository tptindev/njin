#pragma once
#include "_mod.h"
#include "_random.h"
#include "_types.h"
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <initializer_list>
#include <span>

namespace njin {
// Opaque: created with njin_create (njin.h), only accessed through the
// functions below.
struct njin_ctx;

/// @addtogroup grp_module
/// @{

/// Returns the EnTT registry holding every entity and component of the game.
///
/// EnTT is part of the module API, so a system may define its own components
/// and query the registry directly.
/// @param ctx Engine context.
/// @return The game's registry.
entt::registry &world(njin_ctx &ctx);

/// Returns the EnTT dispatcher for sending and receiving events between systems.
///
/// Events that were `enqueue`d are dispatched right after `phase_post_update`.
/// @param ctx Engine context.
/// @return The game's dispatcher.
entt::dispatcher &events(njin_ctx &ctx);

/// Adds a system to the schedule of a phase.
///
/// Valid only inside a module's `setup` callback. A call anywhere else is
/// ignored and logs a warning. Modules run in registration order.
/// @param ctx Engine context.
/// @param phase Phase the system runs in.
/// @param fnc System function.
/// @param name System name, shown in njin_inspector. May be null.
void ecs_register(njin_ctx &ctx, sys_phase phase, sys_fnc fnc, const char *name = nullptr);

/// Adds a system with ordering constraints to the schedule of a phase.
///
/// Within one module and phase, systems are sorted by sys_desc (see
/// _mod.h): `after`/`before` constraints are guaranteed first, then the system
/// with the smaller `order` runs first, and ties follow registration order.
/// @param ctx Engine context.
/// @param phase Phase the system runs in.
/// @param desc Description of the system and its order.
void ecs_register(njin_ctx &ctx, sys_phase phase, const sys_desc &desc);

/// Runs the module's `setup` and puts its systems into the schedule.
///
/// Must be called before njin_run(). A module name can be registered only once.
/// @param ctx Engine context.
/// @param desc Module description.
void njin_mod_register(njin_ctx &ctx, const mod_desc &desc);

/// Registers several modules at once, in the exact order of the list.
///
/// Identical to calling njin_mod_register() for each module in turn: an earlier
/// module runs first within the same phase, and a failing module (duplicate name,
/// registered after njin_run()) is skipped on its own.
/// @code
/// njin::njin_mod_register(*ctx, {input_module(), physics_module(), ui_module()});
/// @endcode
/// @param ctx Engine context.
/// @param mods The modules, in registration order.
void njin_mod_register(njin_ctx &ctx, std::initializer_list<mod_desc> mods);

/// Registers several modules from a list built at run time, for example a
/// `std::vector<mod_desc>` or `std::array`. Same rules as the overload that
/// takes a direct list.
/// @param ctx Engine context.
/// @param mods The modules, in registration order.
void njin_mod_register(njin_ctx &ctx, std::span<const mod_desc> mods);
/// @}

/// @addtogroup grp_time
/// @{

/// Time of the previous frame, in seconds, multiplied by the time scale.
///
/// Multiply velocity by this value so movement does not depend on FPS. Equals 0
/// while paused (time_set_paused()). In `phase_fixed_update` this function returns
/// exactly one fixed step, see fixed_delta().
/// @param ctx Engine context.
/// @return Frame time, in seconds.
f32 delta(const njin_ctx &ctx);

/// Real time of the previous frame, unaffected by the time scale or by pausing.
/// Use it for things that must keep running while the game is stopped, such as the pause menu.
/// @param ctx Engine context.
/// @return Real frame time, in seconds.
f32 delta_real(const njin_ctx &ctx);

/// Sets the game's time scale. 1 is normal, 0.5 is half speed.
///
/// Affects delta(), `phase_fixed_update` and sprite animation. A negative value
/// is treated as 0.
/// @param ctx Engine context.
/// @param scale Time scale.
void time_set_scale(njin_ctx &ctx, f32 scale);

/// Current time scale.
/// @param ctx Engine context.
/// @return Time scale, 1 by default.
f32 time_scale(const njin_ctx &ctx);

/// Pauses or resumes the game's time.
///
/// While paused, delta() returns 0 and `phase_fixed_update` does not run, but the
/// other phases still run every frame: input can still be read, menus can still
/// be drawn. The time scale that was set is kept.
/// @param ctx Engine context.
/// @param paused `true` to pause.
void time_set_paused(njin_ctx &ctx, bool paused);

/// Whether the game is paused.
/// @param ctx Engine context.
/// @return `true` if paused.
bool time_paused(const njin_ctx &ctx);

/// Length of one `phase_fixed_update` step, equal to `1 / njin_cfg::fixed_hz`.
/// @param ctx Engine context.
/// @return Length of one step, in seconds.
f32 fixed_delta(const njin_ctx &ctx);

/// The part of a fixed step left over after this frame's `phase_fixed_update`, from 0 to 1.
///
/// Use it to interpolate positions when drawing, for smooth motion even when the
/// FPS differs from the physics rate:
/// `draw_pos = lerp(prev_pos, pos, fixed_alpha(ctx))`.
/// @param ctx Engine context.
/// @return Ratio from 0 to 1.
f32 fixed_alpha(const njin_ctx &ctx);

/// Time elapsed since the window was opened, in seconds.
/// @param ctx Engine context.
/// @return Running time, in seconds.
f32 elapsed(const njin_ctx &ctx);
/// @}

/// @addtogroup grp_random
/// @{

/// The engine's shared random number generator.
///
/// Seeded from the time at game start, so every run differs.
/// Call `random(ctx).reseed(n)` to get a repeatable sequence, for example when debugging.
/// @param ctx Engine context.
/// @return The random number generator.
rng &random(njin_ctx &ctx);
/// @}
} // namespace njin
