// The inspector's state: what the game last reported, the connection to it,
// and the small amount of UI state the panels keep between frames.
#pragma once
#include "njin_json.h"
#include "njin_net.h"
#include "sysmon.h"
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace inspector {
using njin::json_value;

// Must match the game's debug module (modules/debug.cpp).
constexpr int protocol_version = 2;
constexpr size_t frame_history = 600;
constexpr size_t log_history = 5000;

struct collider_row {
  bool circle = false;
  float x = 0, y = 0, w = 0, h = 0;
  bool trigger = false, enabled = true;
};

// Milliseconds one phase of the frame took, and one system.
struct phase_row {
  std::string name;
  float ms = 0;
};
struct system_row {
  int phase = 0;
  std::string name;
  float ms = 0, avg = 0, peak = 0;
  int calls = 0;
};

// One component type's memory, and one loaded resource.
struct mem_type_row {
  std::string name;
  long long count = 0, size = 0, heap = 0, bytes = 0;
  bool known = false; // its size is known (registered)
};
struct res_row {
  std::string kind, name, info;
  long long bytes = 0;
  bool gpu = false;
};

// What the operating system reports about the game process, over time.
struct usage_history {
  std::vector<double> t; // seconds since the inspector started
  std::vector<double> cpu, cpu_core, ram, ram_private, gpu, gpu_3d, vram, shared;
  void clear() {
    t.clear(); cpu.clear(); cpu_core.clear(); ram.clear(); ram_private.clear();
    gpu.clear(); gpu_3d.clear(); vram.clear(); shared.clear();
  }
};

struct entity_row {
  uint32_t id = 0;
  long long ram = 0, gpu_mem = 0; // bytes: components and their heap, GPU images
  std::string label;
  std::vector<int> comps;
  bool has_pos = false;
  float x = 0, y = 0;
  bool has_col = false;
  collider_row col;
};

struct tilemap_row {
  uint32_t id = 0;
  float x = 0, y = 0, w = 0, h = 0;
  bool solid = false;
};

struct log_row {
  int level = 2;
  std::string src, msg;
};

struct app {
  // Connection.
  std::string host = "127.0.0.1";
  int port = 7779;
  njin::net_link link;
  njin::net_socket connecting;
  float retry = 0.0f;
  bool handshake = false;
  std::string game_title;
  std::string problem; // shown in the status bar (e.g. protocol mismatch)

  // Game state, as last reported.
  std::deque<float> frames;
  long long entity_count = 0;
  bool paused = false;
  float scale = 1.0f;
  std::string scene;
  bool collision_debug = false;
  long long dropped = 0;

  std::vector<std::string> types;
  std::vector<entity_row> ents;
  bool truncated = false;
  float cam[4] = {0, 0, 0, 0};
  bool has_cam = false;
  std::vector<tilemap_row> maps;

  long long selected = -1;
  json_value selected_detail;
  json_value watches;
  std::deque<log_row> logs;

  // Consumption: the game's own timings, memory and resources...
  std::vector<phase_row> phases;
  std::vector<system_row> systems;
  float prof_frame = 0; // ms
  std::vector<mem_type_row> mem_types;
  long long mem_components = 0, mem_registry = 0, mem_entities = 0;
  std::vector<res_row> resources;
  long long res_gpu = 0, res_ram = 0;
  // ...and what the OS reports about the process.
  sysmon mon;
  uint32_t game_pid = 0;
  std::string engine_version; // of the game's njin build
  double clock = 0; // seconds since start
  usage_history usage;

  // UI.
  char entity_filter[128] = "";
  char log_filter[128] = "";
  int log_min_level = 0;
  bool log_autoscroll = true;
  float view_x = 0, view_y = 0, zoom = 1.0f; // world view camera
  bool follow_game_camera = true;
  char sys_filter[128] = "";
  char mem_filter[128] = "";
  char res_filter[128] = "";
  int ent_sort = 0; // 0 id, 1 RAM, 2 GPU
  bool sys_hide_idle = true;
  int layout = 0;            // 0 overview, 1 consumption
  bool layout_dirty = true;  // windows take their place for the current layout this frame
  bool fitted = false;
  std::unordered_map<std::string, float> edits; // live edit buffers, by field key
  std::string active_edit;                      // field being dragged right now
};

// Sends a command to the game, if connected.
void send_cmd(app &a, const json_value &cmd);
// Selects an entity: asks the game for its components from now on.
void select(app &a, long long id);
// Connects (retrying), reads what the game sent and updates `a`. Call once
// per frame with the frame time.
void update_connection(app &a, float dt);
} // namespace inspector
