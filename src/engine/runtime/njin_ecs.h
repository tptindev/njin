#pragma once

#include "_mod.h"
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <string>
#include <vector>

namespace njin {
// One entry of the final run order. `scene` 0 runs in every scene.
struct scheduled_system {
  sys_fnc fnc = nullptr;
  scene_handle scene{};
};

struct ecs_store {
  entt::registry registry;
  entt::dispatcher dispatcher;
  // Final run order per phase, built as modules are registered.
  std::vector<scheduled_system> schedule[phase_count];
  // Systems collected from the module whose setup is currently running.
  std::vector<sys_desc> pending[phase_count];
  std::vector<std::string> modules;
  bool in_setup = false;
  bool started = false;
};

void ecs_run(njin_ctx &ctx, sys_phase phase);
} // namespace njin
