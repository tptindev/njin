#pragma once
#include "_types.h"
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_module
/// @{

/// A system is a plain function that takes the engine context.
///
/// A system is registered into a sys_phase with ecs_register() and is called each
/// time that phase runs.
using sys_fnc = void (*)(njin_ctx &ctx);

/// The phases of a frame, running in exactly the declared order.
///
/// `phase_startup` runs once before the first frame and `phase_shutdown` runs
/// once after the window closes. The other phases run every frame.
///
/// `phase_pre_render`, `phase_render` and `phase_post_render` run between the start
/// and the end of drawing. The engine's camera module draws `phase_render` in world
/// space (through the camera in use) and `phase_post_render` in screen
/// space, so use `phase_post_render` for UI.
///
/// `phase_fixed_update` runs at a fixed rate (default 60 times per second, see
/// `njin_cfg::fixed_hz`): 0, 1 or several times in a frame depending on FPS. In
/// this phase delta() returns exactly one step. Put physics here so the result does not
/// depend on FPS.
enum sys_phase {
  phase_startup,      ///< Once, before the first frame.
  phase_pre_update,   ///< Every frame, before the update.
  phase_fixed_update, ///< At a fixed rate, 0 or more times per frame. For physics.
  phase_update,       ///< Every frame, the game's main logic.
  phase_post_update,  ///< Every frame, after the update. Events are dispatched right after this phase.
  phase_pre_render,   ///< Every frame, before drawing the world.
  phase_render,       ///< Every frame, drawing in world space.
  phase_post_render,  ///< Every frame, drawing in screen space (UI).
  phase_shutdown,     ///< Once, after the window closes.
  phase_count         ///< Number of phases. Not a real phase.
};

/// Describes a system with ordering constraints, used with ecs_register().
///
/// Only compared against systems registered by the same module in the same phase.
struct sys_desc {
  sys_fnc fnc = nullptr; ///< The system function. Must not be null.
  /// A small value runs first, as long as `after`/`before` allow.
  i32 order = 100;
  /// Systems that must run before this system.
  std::vector<sys_fnc> after{};
  /// Systems that must run after this system.
  std::vector<sys_fnc> before{};
  /// Runs only while this scene is running (see scene_register()). A handle with id 0
  /// runs in every scene.
  scene_handle scene{};
  /// Name shown in njin_inspector (the per-system timing table). If null, the
  /// inspector shows `module/#index`.
  const char *name = nullptr;
};

/// Describes a module: a named group of systems.
///
/// `setup` is called exactly once by njin_mod_register() and must register the
/// module's systems with ecs_register().
struct mod_desc {
  const char *name = nullptr;          ///< Module name, must be unique.
  void (*setup)(njin_ctx &ctx) = nullptr; ///< Registers the module's systems.
};
/// @}
} // namespace njin
