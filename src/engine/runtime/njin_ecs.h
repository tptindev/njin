#pragma once
#include "njin_internal_only.h"

#include "_mod.h"
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <string>
#include <vector>

namespace njin {
// One entry of the final run order. `scene` 0 runs in every scene.
// Time the debug inspector shows for one system. Only measured while an
// inspector is connected (ecs_store::profile).
struct sys_stat {
  f32 accum = 0.0f; // ms spent this frame so far (fixed_update may run several times)
  f32 last = 0.0f;  // ms in the last finished frame
  f32 avg = 0.0f;   // smoothed
  f32 peak = 0.0f;  // slowly decaying maximum
  u32 calls = 0;    // calls in the last finished frame
  u32 calls_accum = 0;
};

struct scheduled_system {
  sys_fnc fnc = nullptr;
  scene_handle scene{};
  std::string label; // "module/name", for the inspector
  sys_stat stat;
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
  // Time each system while an inspector is connected.
  bool profile = false;
  // ms spent per phase this frame, and in the last finished frame.
  f32 phase_accum[phase_count] = {};
  f32 phase_last[phase_count] = {};
};

// Moves this frame's timings into `last` and the smoothed values, and starts
// a new frame. Call once per frame, before any system runs.
void ecs_profile_roll(ecs_store &ecs);

void ecs_run(context &ctx, sys_phase phase);
} // namespace njin
