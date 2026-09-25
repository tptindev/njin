#pragma once
#include "_types.h"
#include <vector>

namespace njin {
struct njin_ctx;

// A system is a plain function that receives the engine context.
using sys_fnc = void (*)(njin_ctx &ctx);

// Phases run in this order. startup runs once before the first frame and
// shutdown once after the window closes. The others run every frame.
// pre_render/render/post_render run between begin/end drawing; the core
// camera module draws render in world space (through the active camera) and
// post_render in screen space, so use post_render for UI.
enum sys_phase {
  phase_startup,
  phase_pre_update,
  phase_update,
  phase_post_update,
  phase_pre_render,
  phase_render,
  phase_post_render,
  phase_shutdown,
  phase_count
};

struct sys_desc {
  sys_fnc fnc = nullptr;
  // Lower values run first, as long as after/before allow it.
  i32 order = 100;
  // Systems registered by the same module in the same phase.
  std::vector<sys_fnc> after{};
  std::vector<sys_fnc> before{};
};

// setup is called once by njin_mod_register and registers the module's
// systems with ecs_register.
struct mod_desc {
  const char *name = nullptr;
  void (*setup)(njin_ctx &ctx) = nullptr;
};
} // namespace njin
