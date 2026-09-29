#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"
#include "njin_debug.h"
#include "njin_gif.h"
#include "njin_net.h"
#include <functional>
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
  std::size_t bytes = 0; // sizeof the component; 0 when unknown
  // Heap the component owns (vectors, strings), for the memory tables.
  std::function<std::size_t(const entt::registry &, entt::entity)> heap;
};

// The screen recording the inspector asks for: the game grabs the finished frame every 1/fps seconds, shrinks it
// and appends it to an animated GIF in its save folder (recordings/rec_<date>_<time>.gif).
struct debug_recorder {
  bool active = false;
  bool dirty = false;        // state changed since the inspector was told
  gif_writer gif;
  std::string path;          // file being written, or the last one finished
  std::string error;         // why the last start or write failed
  f32 fps = 15.0f;
  f32 scale = 0.5f;          // of the window size
  f32 max_seconds = 30.0f;
  f32 since_last = 0.0f;     // real seconds since the last grabbed frame
  f32 elapsed = 0.0f;        // real seconds recorded
  f32 owed_cs = 0.0f;        // fraction of a hundredth of a second not yet given to a frame
  i32 frames = 0;
  i32 width = 0, height = 0; // of the GIF
  std::vector<u8> shrunk;    // scratch RGBA of one frame
};

struct debug_state {
  // Unhooks the log tap: the stores destroyed after this one still log.
  ~debug_state();
  debug_state() = default;
  debug_state(const debug_state &) = delete;
  debug_state &operator=(const debug_state &) = delete;

  bool running = false;
  debug_server_desc desc{};
  net_socket listener;
  net_link link;

  // Selection and snapshot pacing.
  i64 selected = -1; // entt integral id, or -1
  f32 snapshot_timer = 0.0f;
  f32 slow_timer = 0.0f; // paces the memory and asset tables (1 Hz)
  std::vector<f32> frame_ms; // since the last stats message

  // Frame stepping while paused: the step frame runs unpaused, the next
  // frame pauses again.
  bool step_pending = false;
  bool stepping = false;

  std::unordered_map<entt::id_type, debug_type_entry> types;
  std::vector<std::pair<std::string, json_value>> watches; // in first-set order
  std::vector<std::string> log_lines; // already serialized, waiting to send

  debug_recorder rec;
};

// Screen recording (see debug_recorder). Start and stop come from the inspector; debug_record_frame() is called
// once per frame, after the world is drawn, and grabs a frame when one is due.
bool debug_record_start(context &ctx, f32 fps, f32 scale, f32 max_seconds);
void debug_record_stop(context &ctx);
void debug_record_frame(context &ctx);
// The `rec` message for the inspector.
json_value debug_record_status(const context &ctx);
} // namespace njin
