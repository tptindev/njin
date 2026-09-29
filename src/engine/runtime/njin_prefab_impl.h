#pragma once
#include "njin_internal_only.h"

#include "njin_prefab.h"
#include <string>
#include <vector>

namespace njin {
struct prefab_slot {
  std::string name;
  prefab_fnc build = nullptr;
  bool scene_owned = true;
};

// Handle id N maps to prefabs[N - 1]. Prefabs are never removed.
struct prefab_store {
  std::vector<prefab_slot> prefabs;
};

// prefab_spawn with a hook that runs after the transform and scene_owned are
// attached but before the build function, so loaders can hand the build
// function data (the level loader attaches level_object here).
entt::entity prefab_spawn_prepared(context &ctx, prefab_handle prefab,
                                   const transform &at,
                                   void (*prepare)(context &, entt::entity, void *),
                                   void *user);
} // namespace njin
