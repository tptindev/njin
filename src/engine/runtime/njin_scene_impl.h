#pragma once

#include "njin_scene.h"
#include <string>
#include <vector>

namespace njin {
struct scene_slot {
  std::string name;
  sys_fnc on_enter = nullptr;
  sys_fnc on_exit = nullptr;
};

// Stages of a scene_fade. `covered` lasts exactly one drawn frame at full
// cover, so the loading screen reaches the screen before the switch (and the
// new scene's on_enter, which may block while loading) runs.
enum class fade_stage { none, out, covered, hold, in };

struct scene_fade_state {
  fade_stage stage = fade_stage::none;
  scene_transition transition{};
  scene_handle target{};
  f32 time = 0.0f;  // seconds spent in the current stage
  f32 cover = 0.0f; // 0 clear .. 1 fully covered
};

// Handle id N maps to scenes[N - 1]. A switch is only requested by scene_set
// (or by a fade reaching full cover) and carried out by scene_store_apply at
// the top of the next frame, so a frame never runs half in one scene and
// half in another.
struct scene_store {
  std::vector<scene_slot> scenes;
  scene_handle current{};
  scene_handle pending{};
  bool has_pending = false;
  scene_fade_state fade;
};

// Advances a running fade by `dt_real` and requests the switch once the
// screen is covered. Then runs a pending switch: on_exit of the old scene,
// destroys the entities it owned, then on_enter of the new one.
void scene_store_apply(njin_ctx &ctx);

// Draws the fade overlay and the loading screen, in screen space, on top of
// everything else. Called after phase_post_render.
void scene_fade_draw(njin_ctx &ctx);
} // namespace njin
