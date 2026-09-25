#pragma once

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
} // namespace njin
