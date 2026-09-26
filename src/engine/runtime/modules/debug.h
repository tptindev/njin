#pragma once
#include "_mod.h"
#include "njin_debug.h"
#include "njin_net.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace njin {
// Core module for the debug link (njin_debug.h). Does nothing until
// debug_server_start. Then, in phase_pre_update, accepts the inspector and
// applies its commands (time control, selection, edits) before any game
// system runs; in phase_post_update it sends frame times, the entity list,
// the selected entity, watches and the log at `snapshot_hz`.
mod_desc debug_module();

struct debug_type_entry {
  std::string name;
  debug_component_fn fn;
};

struct debug_state {
  bool running = false;
  debug_server_desc desc{};
  net_socket listener;
  net_link link;

  // Selection and snapshot pacing.
  i64 selected = -1; // entt integral id, or -1
  f32 snapshot_timer = 0.0f;
  std::vector<f32> frame_ms; // since the last stats message

  // Frame stepping while paused: the step frame runs unpaused, the next
  // frame pauses again.
  bool step_pending = false;
  bool stepping = false;

  std::unordered_map<entt::id_type, debug_type_entry> types;
  std::vector<std::pair<std::string, json_value>> watches; // in first-set order
  std::vector<std::string> log_lines; // already serialized, waiting to send
};
} // namespace njin
