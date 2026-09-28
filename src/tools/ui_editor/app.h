#pragma once
#include "imgui.h"
#include "njin_json.h"
#include "njin_ui_layout.h"
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace njin {
struct njin_ctx;
}

namespace ui_editor {
using njin::json_value;
using njin::rect;
using njin::ui_layout;
using njin::ui_panel_data;
using njin::ui_popup_data;
using njin::ui_widget_data;
using njin::ui_widget_kind;
using njin::vec2;

struct resolution_preset {
  const char *name;
  vec2 size;
};

inline const resolution_preset k_res_presets[] = {
    {"1920 x 1080", {1920.0f, 1080.0f}}, {"1280 x 720", {1280.0f, 720.0f}},
    {"960 x 540", {960.0f, 540.0f}},     {"640 x 360", {640.0f, 360.0f}},
    {"480 x 270", {480.0f, 270.0f}},     {"320 x 180", {320.0f, 180.0f}},
};

// A node of the scene tree: the layout root, a panel, a widget of a panel, or a
// popup. Indices into ui_layout; they go stale when the vectors change, so
// every edit goes through editor_app and fixes the selection.
struct node_ref {
  enum kind_t { none, root, panel, widget, popup } kind = none;
  int pi = -1; // panel index (panel, widget)
  int wi = -1; // widget index in the panel
  int qi = -1; // popup index

  static node_ref make_root() { return {root, -1, -1, -1}; }
  static node_ref make_panel(int p) { return {panel, p, -1, -1}; }
  static node_ref make_widget(int p, int w) { return {widget, p, w, -1}; }
  static node_ref make_popup(int i) { return {popup, -1, -1, i}; }
  bool operator==(const node_ref &o) const {
    return kind == o.kind && pi == o.pi && wi == o.wi && qi == o.qi;
  }
};

// What the "Nodes" palette drags: a node type to create.
enum class new_node : int {
  panel = 100,
  popup = 101,
  // widgets: the ui_widget_kind value
};

// Drag and drop payload names (ImGui limits them to 32 characters).
inline constexpr const char *k_payload_new = "NJIN_UI_NEW";   // int: ui_widget_kind or new_node
inline constexpr const char *k_payload_node = "NJIN_UI_NODE"; // node_ref

// Where the preview drew a panel or popup and its widgets, in design pixels.
struct widget_box {
  int index = -1;
  rect area{};
};
struct panel_box {
  node_ref node;
  rect area{};
  std::vector<widget_box> widgets;
};

struct editor_app {
  njin::njin_ctx *ctx = nullptr;

  ui_layout layout;
  std::string file_path;

  node_ref sel = node_ref::make_root();

  // Play mode runs a copy, so pressing buttons and dragging sliders never edits
  // the document (like running the scene in Godot).
  bool play_mode = false;
  ui_layout play_layout;
  std::deque<std::string> output;

  // Preview used when the layout inherits the game's style (custom_style off).
  int preview_style = 0; // 0 default, 1 pixel

  // Viewport
  float zoom = 1.0f;
  ImVec2 pan{0.0f, 0.0f};
  bool fitted = false;
  bool show_grid = true;
  bool show_anchors = true;
  bool snap = true;
  float snap_size = 8.0f;
  std::vector<panel_box> boxes; // from the last preview render
  int preview_k = 1;             // the preview texture is k times the design size
  // Where the Viewport put the image last frame, for the play-mode mouse.
  ImVec2 view_image_min{0.0f, 0.0f};
  bool view_hovered = false;
  bool view_focused = false;
  bool scene_focused = false;

  // Windows
  bool show_scene = true, show_nodes = true, show_viewport = true, show_inspector = true;
  bool show_theme = true, show_code = true, show_json = true, show_output = true;
  bool reset_dock = false;

  std::string status_text = "Sẵn sàng";

  // Undo: the layout as JSON text at each commit. A commit happens at the end
  // of a frame in which nothing is being dragged or typed and the layout
  // differs from the last one, so one slider drag or one text edit is one step.
  std::string committed;
  std::string saved;
  std::vector<std::string> undo_stack;
  std::vector<std::string> redo_stack;
  static constexpr size_t k_max_history = 100;

  // Structural edits requested while a window iterates the layout run at the
  // end of the frame.
  std::vector<std::function<void()>> deferred;

  // Textures for image widgets, by path; 0 when the load failed.
  std::map<std::string, njin::texture_handle> textures;

  bool dirty() const { return committed != saved; }
  void set_status(const std::string &msg) { status_text = msg; }
  void log(const std::string &msg);
  void defer(std::function<void()> fn) { deferred.push_back(std::move(fn)); }

  void end_frame(bool busy);
  void reset_history();
  void undo();
  void redo();

  // Selection
  ui_panel_data *panel_at(int i);
  ui_widget_data *widget_at(const node_ref &n);
  ui_popup_data *popup_at(int i);
  ui_panel_data *current_panel() { return panel_at(sel.pi); }
  ui_widget_data *current_widget() { return widget_at(sel); }
  ui_popup_data *current_popup() { return sel.kind == node_ref::popup ? popup_at(sel.qi) : nullptr; }
  void select(const node_ref &n) { sel = n; }
  void fix_selection();
  std::string node_name(const node_ref &n);

  // Editing. Indices are clamped; each returns the node it created or moved.
  std::string unique_id(const char *prefix) const;
  node_ref add_widget(int panel, int index, ui_widget_kind kind);
  node_ref add_panel(vec2 center_in_design);
  node_ref add_popup();
  node_ref add_new(int what, const node_ref &target);
  node_ref move_widget(const node_ref &from, int to_panel, int to_index);
  node_ref move_panel(int from, int to);
  node_ref move_popup(int from, int to);
  node_ref duplicate(const node_ref &n);
  void remove(const node_ref &n);
  void set_widget_kind(ui_widget_data &w, ui_widget_kind kind);

  void enter_play();
  void leave_play();
  ui_layout &shown_layout() { return play_mode ? play_layout : layout; }

  void new_layout();
  bool open_file(const std::string &path);
  bool save_file(const std::string &path);
  void add_default_demo();
};

// Widget type names and colours, shared by the scene tree, the palette and the
// inspector.
const char *kind_name(ui_widget_kind k);
const char *kind_prefix(ui_widget_kind k);
ImVec4 kind_color(ui_widget_kind k);
inline constexpr ui_widget_kind k_all_kinds[] = {
    ui_widget_kind::label,    ui_widget_kind::button, ui_widget_kind::toggle,
    ui_widget_kind::slider,   ui_widget_kind::choice, ui_widget_kind::progress,
    ui_widget_kind::image,    ui_widget_kind::keybind, ui_widget_kind::row,
    ui_widget_kind::space,
};

// std::string input for ImGui, which only knows char buffers.
bool input_string(const char *label, std::string &s, ImGuiInputTextFlags flags = 0);
bool input_string_multiline(const char *label, std::string &s, ImVec2 size);

} // namespace ui_editor
