#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"
#include "njin_timer.h"
#include <vector>

namespace njin {
// Core module. In phase_update, before any game module, fires due timers and
// advances tweens. Both drop themselves when their owner entity is gone, and
// (unless told otherwise) when the scene they were made in is left.
mod_desc timer_module();

struct timer_job {
  u32 id = 0;
  f32 left = 0.0f;
  f32 interval = 0.0f;
  i32 remaining = 1; // calls still to make, -1 forever
  timer_desc desc{};
  bool owned = false;
  u32 scene = 0;
  std::function<void(njin_ctx &)> fn;
  bool dead = false;
};

// What a tween drives. Custom ones go through `apply`.
enum class tween_prop : u8 { pos, scale, rot, tint, custom };

struct tween_job {
  u32 id = 0;
  entt::entity owner = entt::null;
  bool owned = false;
  tween_prop prop = tween_prop::custom;
  ease curve = ease::linear;
  tween_desc desc{};
  u32 scene = 0;
  f32 duration = 0.0f;
  f32 time = 0.0f;  // into the current run
  f32 wait = 0.0f;  // delay still to go
  i32 runs_left = 0;
  bool forward = true;
  bool started = false; // `from` captured
  vec4 from{}, to{};
  std::function<void(njin_ctx &, f32)> apply;
  bool dead = false;
};

struct timer_state {
  std::vector<timer_job> timers;
  std::vector<tween_job> tweens;
  u32 next_id = 1;
};
} // namespace njin
