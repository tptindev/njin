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

// Handle id N maps to scenes[N - 1]. A switch is only requested by scene_set
// and carried out by scene_store_apply at the top of the next frame, so a
// frame never runs half in one scene and half in another.
struct scene_store {
  std::vector<scene_slot> scenes;
  scene_handle current{};
  scene_handle pending{};
  bool has_pending = false;
};

// Runs a pending switch: on_exit of the old scene, destroys the entities it
// owned, then on_enter of the new one.
void scene_store_apply(njin_ctx &ctx);
} // namespace njin
