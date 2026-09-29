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
constexpr int protocol_version = 5;
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

// One draw of the game's last 3D pass (render3d.h, debug3d_item): shown as a
// point where it is, in its colour.
struct item3d_row {
  int kind = 0;
  float color[4] = {1, 1, 1, 1};
  float pos[3] = {};
};

// A point (kind 0) or spot (kind 1) light of that pass.
struct light3d_row {
  int kind = 0;
  float pos[3] = {}, color[3] = {1, 1, 1}, dir[3] = {0, -1, 0};
  float radius = 0, cone = 0;
};

// A debug gizmo the game drew (njin_gizmo.h): a line, or a mark (a point
// when `text` is empty, else a label). 2D uses x, y; 3D all three.
struct gizmo_line_row {
  float a[3] = {}, b[3] = {};
  float color[4] = {0, 1, 0, 1};
};
struct gizmo_mark_row {
  float pos[3] = {};
  float color[4] = {1, 1, 1, 1};
  std::string text;
};

// The game's last 3D pass, when it draws in 3D.
struct scene3d_state {
  bool on = false;
  float cam[10] = {}; // position, target, up, fovy
  float aspect = 16.0f / 9.0f;
  float sun[3] = {0, -1, 0};
  std::vector<light3d_row> lights;
  std::vector<item3d_row> items;
  std::vector<gizmo_line_row> gizmo_lines;
  std::vector<gizmo_mark_row> gizmo_marks;
  long long instanced = 0;
};

struct log_row {
  int level = 2;
  std::string src, msg;
};

// The game's screen recording (a GIF it writes to its own save folder), as last reported.
struct recording_state {
  bool on = false;
  int frames = 0, width = 0, height = 0;
  float secs = 0, fps = 0, max_secs = 0;
  long long bytes = 0;
  std::string file, dir; // the current or last finished recording, and its folder
  std::string error;
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
  // What the last frame's world pass drew (see njin's render_stats).
  struct render_row {
    long long sprites = 0, sprites_culled = 0, tile_chunks = 0;
    long long emitters = 0, emitters_culled = 0, particles = 0, particles_gpu = 0;
    long long instanced = 0, batches = 0, post_passes = 0;
  } render;

  std::vector<std::string> types;
  std::vector<entity_row> ents;
  bool truncated = false;
  float cam[4] = {0, 0, 0, 0};
  bool has_cam = false;
  std::vector<tilemap_row> maps;
  std::vector<gizmo_line_row> gizmo_lines; // 2D
  std::vector<gizmo_mark_row> gizmo_marks;
  scene3d_state scene3d;

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

  recording_state rec;

  // UI.
  int rec_fps = 1;    // index into the frame rate choices (10, 15, 20, 30)
  int rec_scale = 2;  // index into the size choices (100%, 75%, 50%, 33%)
  float rec_max = 30; // seconds
  char entity_filter[128] = "";
  char log_filter[128] = "";
  int log_min_level = 0;
  bool log_autoscroll = true;
  float view_x = 0, view_y = 0, zoom = 1.0f; // world view camera, 2D
  bool follow_game_camera = true;
  int world_mode = 0; // 0 auto (3D when the game draws in 3D), 1 2D, 2 3D
  // World view camera, 3D: orbits `orbit_target` at `orbit_dist`.
  float orbit_yaw = 45.0f, orbit_pitch = 35.0f, orbit_dist = 20.0f;
  float orbit_target[3] = {0, 0, 0};
  bool orbit_fitted = false;
  bool follow_game_camera_3d = false; // look through the game's eye instead of orbiting
  char sys_filter[128] = "";
  char mem_filter[128] = "";
  char res_filter[128] = "";
  int ent_sort = 0; // 0 id, 1 RAM, 2 GPU
  bool sys_hide_idle = true;
  int layout = 0;            // 0 overview, 1 consumption
  bool layout_dirty = true;  // windows take their place for the current layout this frame
  int layout_dirty_age = 0;  // frames the placement has been forced: two, so the menu bar's height is known
  // The window area (below the menu bar) as of the last frame, to notice a resize (then `rescale` is true for that
  // frame), and where each panel is as a share of that area, so a panel keeps its place when the window changes.
  float work_w = 0, work_h = 0;
  bool rescale = false;
  struct panel_share {
    float x = 0, y = 0, w = 0, h = 0;
  };
  std::unordered_map<std::string, panel_share> shares;
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
