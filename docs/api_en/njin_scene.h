#pragma once
#include "_mod.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_scene
/// @{

/// Description of a scene, used with scene_register().
///
/// A scene is a major state of the game: menu, playing, game over. Only one
/// scene runs at a time.
struct scene_desc {
  const char *name = nullptr;  ///< Scene name, must be unique.
  sys_fnc on_enter = nullptr;  ///< Called once when entering the scene. May be null.
  sys_fnc on_exit = nullptr;   ///< Called once when leaving the scene. May be null.
};

/// Register a scene. If the name already exists, returns the existing scene.
///
/// Can be used at any time, including in a module's `setup`: register the scene
/// first, then use the handle in njin::sys_desc so a system only runs in that scene.
/// @param ctx Engine context.
/// @param desc Scene description.
/// @return Handle of the scene, or a handle with id 0 if `desc.name` is null.
scene_handle scene_register(njin_ctx &ctx, const scene_desc &desc);

/// Find a scene by name.
/// @param ctx Engine context.
/// @param name Scene name.
/// @return Handle of the scene, or a handle with id 0 if there is none.
scene_handle scene_find(const njin_ctx &ctx, const char *name);

/// Switch to another scene.
///
/// The switch happens at the **start of the next frame**, not immediately, so
/// the current frame runs to completion. When switching, the engine in turn:
/// 1. calls `on_exit` of the old scene,
/// 2. destroys every entity whose njin::scene_owned points to the old scene,
/// 3. calls `on_enter` of the new scene.
///
/// If called several times in one frame, the last call wins. Switching to the
/// scene that is already running does nothing.
/// @param ctx Engine context.
/// @param scene Target scene.
void scene_set(njin_ctx &ctx, scene_handle scene);

/// The scene that is running.
/// @param ctx Engine context.
/// @return The running scene, or a handle with id 0 if there is none yet.
scene_handle scene_current(const njin_ctx &ctx);

/// Scene transition effect, used with scene_fade().
///
/// The screen is gradually covered with `color` over `fade_out` seconds, the
/// scene is switched once fully covered, the screen is held covered for at least
/// `hold` seconds, then gradually uncovered over `fade_in` seconds. Times are in
/// real time: not affected by pause or time_set_scale().
struct scene_transition {
  f32 fade_out = 0.35f; ///< Time to cover the screen, seconds.
  f32 hold = 0.0f;      ///< Minimum time to hold the screen covered, seconds.
  f32 fade_in = 0.35f;  ///< Time to uncover the screen, seconds.
  rgba color{0.0f, 0.0f, 0.0f, 1.0f}; ///< Cover color. Black by default.
  /// Draws a loading screen while fully covered, in screen space, on top of the
  /// cover color. May be null.
  ///
  /// Drawn for at least one frame **before** the new scene's `on_enter` runs,
  /// so if `on_enter` loads heavy resources the player still sees this screen
  /// instead of a frozen window.
  sys_fnc draw_loading = nullptr;
};

/// Switch to another scene with a fade effect.
///
/// Like scene_set(), but the scene changes once the screen is fully covered. The
/// game keeps running during the transition: use scene_transitioning() to skip
/// input if needed.
///
/// Calling it again during a transition only changes the target scene (and the
/// effect), without restarting from the beginning; if the screen is being
/// uncovered, it is covered again starting from the current coverage. Calling
/// scene_set() during a transition cancels the effect and switches scene on the
/// next frame.
/// @param ctx Engine context.
/// @param scene Target scene.
/// @param transition Effect.
void scene_fade(njin_ctx &ctx, scene_handle scene,
                const scene_transition &transition = {});

/// Whether a scene_fade() transition is in progress.
/// @param ctx Engine context.
/// @return `true` from the scene_fade() call until the screen is fully uncovered.
bool scene_transitioning(const njin_ctx &ctx);

/// Current coverage of the scene transition effect.
/// @param ctx Engine context.
/// @return 0 is not covered, 1 is fully covered.
f32 scene_transition_cover(const njin_ctx &ctx);
/// @}
} // namespace njin
