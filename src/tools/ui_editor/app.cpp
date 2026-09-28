#include "app.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <set>

namespace ui_editor {

namespace {
std::string dump(const ui_layout &layout) { return njin::json_dump(njin::ui_layout_to_json(layout), true); }

template <typename T> void move_item(std::vector<T> &v, int from, int to) {
  // `to` is an insertion index in the vector as it is before the move.
  if (from < 0 || from >= (int)v.size())
    return;
  to = std::clamp(to, 0, (int)v.size());
  if (to == from || to == from + 1)
    return;
  T item = std::move(v[(size_t)from]);
  v.erase(v.begin() + from);
  if (to > from)
    to--;
  v.insert(v.begin() + to, std::move(item));
}
} // namespace

const char *kind_name(ui_widget_kind k) {
  switch (k) {
  case ui_widget_kind::label: return "Label";
  case ui_widget_kind::space: return "Space";
  case ui_widget_kind::button: return "Button";
  case ui_widget_kind::toggle: return "Toggle";
  case ui_widget_kind::slider: return "Slider";
  case ui_widget_kind::choice: return "Choice";
  case ui_widget_kind::progress: return "Progress";
  case ui_widget_kind::image: return "Image";
  case ui_widget_kind::row: return "Row";
  case ui_widget_kind::keybind: return "Keybind";
  }
  return "?";
}

const char *kind_prefix(ui_widget_kind k) {
  switch (k) {
  case ui_widget_kind::label: return "lbl";
  case ui_widget_kind::space: return "space";
  case ui_widget_kind::button: return "btn";
  case ui_widget_kind::toggle: return "tog";
  case ui_widget_kind::slider: return "sld";
  case ui_widget_kind::choice: return "cho";
  case ui_widget_kind::progress: return "prg";
  case ui_widget_kind::image: return "img";
  case ui_widget_kind::row: return "row";
  case ui_widget_kind::keybind: return "key";
  }
  return "w";
}

ImVec4 kind_color(ui_widget_kind k) {
  switch (k) {
  case ui_widget_kind::button: return {0.35f, 0.65f, 1.0f, 1.0f};
  case ui_widget_kind::label: return {0.85f, 0.85f, 0.85f, 1.0f};
  case ui_widget_kind::space: return {0.55f, 0.55f, 0.55f, 1.0f};
  case ui_widget_kind::toggle: return {0.4f, 0.85f, 0.5f, 1.0f};
  case ui_widget_kind::slider: return {1.0f, 0.7f, 0.25f, 1.0f};
  case ui_widget_kind::choice: return {0.8f, 0.5f, 0.95f, 1.0f};
  case ui_widget_kind::progress: return {0.3f, 0.85f, 0.85f, 1.0f};
  case ui_widget_kind::image: return {0.95f, 0.4f, 0.6f, 1.0f};
  case ui_widget_kind::row: return {0.85f, 0.8f, 0.35f, 1.0f};
  case ui_widget_kind::keybind: return {0.8f, 0.6f, 0.4f, 1.0f};
  }
  return {1, 1, 1, 1};
}

namespace {
int resize_string(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto *s = (std::string *)data->UserData;
    s->resize((size_t)data->BufTextLen);
    data->Buf = s->data();
  }
  return 0;
}
} // namespace

bool input_string(const char *label, std::string &s, ImGuiInputTextFlags flags) {
  return ImGui::InputText(label, s.data(), s.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize,
                          resize_string, &s);
}

bool input_string_multiline(const char *label, std::string &s, ImVec2 size) {
  return ImGui::InputTextMultiline(label, s.data(), s.capacity() + 1, size,
                                   ImGuiInputTextFlags_CallbackResize, resize_string, &s);
}

// --- history ---

void editor_app::log(const std::string &msg) {
  output.push_back(msg);
  while (output.size() > 200)
    output.pop_front();
}

void editor_app::end_frame(bool busy) {
  std::vector<std::function<void()>> ops;
  ops.swap(deferred);
  for (auto &op : ops)
    op();
  fix_selection();
  if (busy)
    return;
  std::string now = dump(layout);
  if (now == committed)
    return;
  undo_stack.push_back(std::move(committed));
  if (undo_stack.size() > k_max_history)
    undo_stack.erase(undo_stack.begin());
  redo_stack.clear();
  committed = std::move(now);
}

void editor_app::reset_history() {
  undo_stack.clear();
  redo_stack.clear();
  committed = dump(layout);
  saved = committed;
}

namespace {
void load_text(ui_layout &layout, const std::string &text) {
  json_value json;
  if (njin::json_parse(text, json))
    njin::ui_layout_parse(json, layout);
}
} // namespace

void editor_app::undo() {
  if (play_mode || undo_stack.empty())
    return;
  redo_stack.push_back(committed);
  committed = undo_stack.back();
  undo_stack.pop_back();
  load_text(layout, committed);
  fix_selection();
  set_status("Hoàn tác (Undo)");
}

void editor_app::redo() {
  if (play_mode || redo_stack.empty())
    return;
  undo_stack.push_back(committed);
  committed = redo_stack.back();
  redo_stack.pop_back();
  load_text(layout, committed);
  fix_selection();
  set_status("Làm lại (Redo)");
}

// --- selection ---

ui_panel_data *editor_app::panel_at(int i) {
  if (i >= 0 && i < (int)layout.panels.size())
    return &layout.panels[(size_t)i];
  return nullptr;
}

ui_widget_data *editor_app::widget_at(const node_ref &n) {
  if (n.kind != node_ref::widget)
    return nullptr;
  if (auto *p = panel_at(n.pi))
    if (n.wi >= 0 && n.wi < (int)p->widgets.size())
      return &p->widgets[(size_t)n.wi];
  return nullptr;
}

ui_popup_data *editor_app::popup_at(int i) {
  if (i >= 0 && i < (int)layout.popups.size())
    return &layout.popups[(size_t)i];
  return nullptr;
}

void editor_app::fix_selection() {
  switch (sel.kind) {
  case node_ref::panel:
    if (!panel_at(sel.pi))
      sel = node_ref::make_root();
    break;
  case node_ref::widget:
    if (!widget_at(sel))
      sel = panel_at(sel.pi) ? node_ref::make_panel(sel.pi) : node_ref::make_root();
    break;
  case node_ref::popup:
    if (!popup_at(sel.qi))
      sel = node_ref::make_root();
    break;
  default:
    break;
  }
}

std::string editor_app::node_name(const node_ref &n) {
  switch (n.kind) {
  case node_ref::root: return "Layout";
  case node_ref::panel: return panel_at(n.pi) ? panel_at(n.pi)->id : "?";
  case node_ref::popup: return popup_at(n.qi) ? popup_at(n.qi)->id : "?";
  case node_ref::widget:
    if (auto *w = widget_at(n))
      return w->id.empty() ? std::string(kind_name(w->kind)) : w->id;
    return "?";
  default: return "";
  }
}

// --- editing ---

std::string editor_app::unique_id(const char *prefix) const {
  std::set<std::string> used;
  for (const auto &p : layout.panels) {
    used.insert(p.id);
    for (const auto &w : p.widgets)
      used.insert(w.id);
  }
  for (const auto &pop : layout.popups)
    used.insert(pop.id);
  for (int i = 1;; ++i) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s_%d", prefix, i);
    if (!used.count(buf))
      return buf;
  }
}

void editor_app::set_widget_kind(ui_widget_data &w, ui_widget_kind kind) {
  w.kind = kind;
  switch (kind) {
  case ui_widget_kind::button:
    if (w.label.empty()) w.label = "Nút bấm";
    break;
  case ui_widget_kind::label:
    if (w.label.empty()) w.label = "Dòng chữ";
    break;
  case ui_widget_kind::toggle:
    if (w.label.empty()) w.label = "Công tắc";
    break;
  case ui_widget_kind::slider:
    if (w.label.empty()) w.label = "Thanh trượt";
    if (w.max_val <= w.min_val) { w.min_val = 0.0f; w.max_val = 1.0f; }
    break;
  case ui_widget_kind::choice:
    if (w.label.empty()) w.label = "Lựa chọn";
    if (w.options.empty()) w.options = {"Mục 1", "Mục 2", "Mục 3"};
    break;
  case ui_widget_kind::progress:
    if (w.float_val <= 0.0f) w.float_val = 0.5f;
    break;
  case ui_widget_kind::keybind:
    if (w.label.empty()) w.label = "Phím";
    break;
  case ui_widget_kind::row:
    if (w.columns < 1) w.columns = 2;
    break;
  case ui_widget_kind::space:
    if (w.height <= 0.0f) w.height = 10.0f;
    break;
  case ui_widget_kind::image:
    break;
  }
}

node_ref editor_app::add_widget(int panel, int index, ui_widget_kind kind) {
  ui_panel_data *p = panel_at(panel);
  if (p == nullptr)
    return sel;
  ui_widget_data w;
  if (kind != ui_widget_kind::space && kind != ui_widget_kind::row)
    w.id = unique_id(kind_prefix(kind));
  set_widget_kind(w, kind);
  index = std::clamp(index, 0, (int)p->widgets.size());
  p->widgets.insert(p->widgets.begin() + index, std::move(w));
  return node_ref::make_widget(panel, index);
}

node_ref editor_app::add_panel(vec2 center) {
  ui_panel_data p;
  p.id = unique_id("panel");
  p.title = "Panel";
  p.anchor = {0.5f, 0.5f};
  p.pivot = {0.5f, 0.5f};
  p.offset = center - layout.design_resolution * p.anchor;
  if (snap && snap_size > 0.0f) {
    p.offset.x = std::round(p.offset.x / snap_size) * snap_size;
    p.offset.y = std::round(p.offset.y / snap_size) * snap_size;
  }
  layout.panels.push_back(std::move(p));
  return node_ref::make_panel((int)layout.panels.size() - 1);
}

node_ref editor_app::add_popup() {
  ui_popup_data pop;
  pop.id = unique_id("popup");
  pop.title = "Thông báo";
  pop.message = "Nội dung hộp thoại";
  pop.buttons = {"OK"};
  pop.open = true;
  layout.popups.push_back(std::move(pop));
  return node_ref::make_popup((int)layout.popups.size() - 1);
}

// Adds a palette item next to or inside `target`, the way Godot's "Add Child
// Node" does: into a panel, after a widget, or a new panel for the root.
node_ref editor_app::add_new(int what, const node_ref &target) {
  if (what == (int)new_node::panel)
    return add_panel(layout.design_resolution * 0.5f);
  if (what == (int)new_node::popup)
    return add_popup();
  const auto kind = (ui_widget_kind)what;
  switch (target.kind) {
  case node_ref::panel:
    return add_widget(target.pi, (int)panel_at(target.pi)->widgets.size(), kind);
  case node_ref::widget:
    return add_widget(target.pi, target.wi + 1, kind);
  default: {
    const node_ref p = add_panel(layout.design_resolution * 0.5f);
    return add_widget(p.pi, 0, kind);
  }
  }
}

node_ref editor_app::move_widget(const node_ref &from, int to_panel, int to_index) {
  ui_widget_data *w = widget_at(from);
  ui_panel_data *dst = panel_at(to_panel);
  if (w == nullptr || dst == nullptr)
    return from;
  if (from.pi == to_panel) {
    auto &v = dst->widgets;
    to_index = std::clamp(to_index, 0, (int)v.size());
    move_item(v, from.wi, to_index);
    const int final_index = to_index > from.wi ? to_index - 1 : to_index;
    return node_ref::make_widget(to_panel, std::clamp(final_index, 0, (int)v.size() - 1));
  }
  ui_widget_data moved = std::move(*w);
  auto &src = layout.panels[(size_t)from.pi].widgets;
  src.erase(src.begin() + from.wi);
  to_index = std::clamp(to_index, 0, (int)dst->widgets.size());
  dst->widgets.insert(dst->widgets.begin() + to_index, std::move(moved));
  return node_ref::make_widget(to_panel, to_index);
}

node_ref editor_app::move_panel(int from, int to) {
  move_item(layout.panels, from, to);
  const int final_index = to > from ? to - 1 : to;
  return node_ref::make_panel(std::clamp(final_index, 0, (int)layout.panels.size() - 1));
}

node_ref editor_app::move_popup(int from, int to) {
  move_item(layout.popups, from, to);
  const int final_index = to > from ? to - 1 : to;
  return node_ref::make_popup(std::clamp(final_index, 0, (int)layout.popups.size() - 1));
}

node_ref editor_app::duplicate(const node_ref &n) {
  switch (n.kind) {
  case node_ref::panel: {
    ui_panel_data dup = layout.panels[(size_t)n.pi];
    dup.id = unique_id(dup.id.c_str());
    dup.offset += vec2{16.0f, 16.0f};
    for (auto &w : dup.widgets)
      if (!w.id.empty())
        w.id = unique_id(w.id.c_str());
    layout.panels.insert(layout.panels.begin() + n.pi + 1, std::move(dup));
    return node_ref::make_panel(n.pi + 1);
  }
  case node_ref::widget: {
    auto &v = layout.panels[(size_t)n.pi].widgets;
    ui_widget_data dup = v[(size_t)n.wi];
    if (!dup.id.empty())
      dup.id = unique_id(dup.id.c_str());
    v.insert(v.begin() + n.wi + 1, std::move(dup));
    return node_ref::make_widget(n.pi, n.wi + 1);
  }
  case node_ref::popup: {
    ui_popup_data dup = layout.popups[(size_t)n.qi];
    dup.id = unique_id(dup.id.c_str());
    layout.popups.insert(layout.popups.begin() + n.qi + 1, std::move(dup));
    return node_ref::make_popup(n.qi + 1);
  }
  default:
    return n;
  }
}

void editor_app::remove(const node_ref &n) {
  switch (n.kind) {
  case node_ref::panel:
    if (panel_at(n.pi)) {
      layout.panels.erase(layout.panels.begin() + n.pi);
      sel = node_ref::make_root();
    }
    break;
  case node_ref::widget:
    if (widget_at(n)) {
      auto &v = layout.panels[(size_t)n.pi].widgets;
      v.erase(v.begin() + n.wi);
      sel = v.empty() ? node_ref::make_panel(n.pi)
                      : node_ref::make_widget(n.pi, std::min(n.wi, (int)v.size() - 1));
    }
    break;
  case node_ref::popup:
    if (popup_at(n.qi)) {
      layout.popups.erase(layout.popups.begin() + n.qi);
      sel = node_ref::make_root();
    }
    break;
  default:
    break;
  }
}

void editor_app::enter_play() {
  if (play_mode)
    return;
  play_layout = layout;
  play_mode = true;
  log("--- Chạy thử (Play) ---");
  set_status("Đang chạy thử: bấm nút, kéo slider trong Viewport. F5 để dừng.");
}

void editor_app::leave_play() {
  if (!play_mode)
    return;
  play_mode = false;
  play_layout = ui_layout{};
  log("--- Dừng ---");
  set_status("Đã quay về chế độ chỉnh sửa");
}

// --- files ---

void editor_app::add_default_demo() {
  layout = ui_layout{};
  layout.design_resolution = {1280.0f, 720.0f};

  ui_panel_data menu;
  menu.id = "main_menu";
  menu.title = "Menu Chính";
  menu.width = 340.0f;
  auto widget = [](ui_widget_kind kind, const char *id, const char *label) {
    ui_widget_data w;
    w.kind = kind;
    w.id = id;
    w.label = label;
    return w;
  };
  menu.widgets.push_back(widget(ui_widget_kind::label, "lbl_subtitle", "Chào mừng đến với njin!"));
  ui_widget_data sp = widget(ui_widget_kind::space, "", "");
  sp.height = 10.0f;
  menu.widgets.push_back(sp);
  menu.widgets.push_back(widget(ui_widget_kind::button, "btn_play", "Bắt đầu chơi"));
  menu.widgets.push_back(widget(ui_widget_kind::button, "btn_settings", "Cài đặt"));
  menu.widgets.push_back(widget(ui_widget_kind::button, "btn_quit", "Thoát"));
  layout.panels.push_back(std::move(menu));

  ui_panel_data settings;
  settings.id = "settings";
  settings.title = "Cài Đặt";
  settings.width = 360.0f;
  settings.visible = false;
  ui_widget_data vol = widget(ui_widget_kind::slider, "sld_volume", "Âm lượng");
  vol.float_val = 0.8f;
  vol.step = 0.05f;
  vol.percent = true;
  settings.widgets.push_back(vol);
  ui_widget_data row = widget(ui_widget_kind::row, "", "");
  row.columns = 2;
  settings.widgets.push_back(row);
  settings.widgets.push_back(widget(ui_widget_kind::toggle, "tog_fullscreen", "Toàn màn hình"));
  ui_widget_data vsync = widget(ui_widget_kind::toggle, "tog_vsync", "V-Sync");
  vsync.bool_val = true;
  settings.widgets.push_back(vsync);
  ui_widget_data diff = widget(ui_widget_kind::choice, "cho_difficulty", "Độ khó");
  diff.int_val = 1;
  diff.options = {"Dễ", "Vừa", "Khó"};
  settings.widgets.push_back(diff);
  settings.widgets.push_back(widget(ui_widget_kind::button, "btn_back", "Quay lại"));
  layout.panels.push_back(std::move(settings));

  ui_popup_data pop;
  pop.id = "quit_confirm";
  pop.title = "Thoát game?";
  pop.message = "Tiến trình chưa lưu sẽ mất.";
  pop.buttons = {"Ở lại", "Thoát"};
  pop.cancel_button = 0;
  layout.popups.push_back(std::move(pop));

  file_path.clear();
  sel = node_ref::make_panel(0);
  fitted = false;
  reset_history();
}

void editor_app::new_layout() {
  leave_play();
  layout = ui_layout{};
  file_path.clear();
  sel = node_ref::make_root();
  fitted = false;
  reset_history();
  set_status("Đã tạo layout mới");
}

bool editor_app::open_file(const std::string &path) {
  json_value json;
  ui_layout loaded;
  if (!njin::json_load(path.c_str(), json) || !njin::ui_layout_parse(json, loaded)) {
    set_status("Lỗi: không đọc được UI layout từ " + path);
    return false;
  }
  leave_play();
  layout = std::move(loaded);
  file_path = path;
  sel = layout.panels.empty() ? node_ref::make_root() : node_ref::make_panel(0);
  fitted = false;
  reset_history();
  set_status("Đã mở " + path);
  return true;
}

bool editor_app::save_file(const std::string &path) {
  if (path.empty())
    return false;
  if (!njin::ui_layout_save(path.c_str(), layout, true)) {
    set_status("Lỗi: không ghi được " + path);
    return false;
  }
  file_path = path;
  saved = committed = dump(layout);
  set_status("Đã lưu " + path);
  return true;
}

} // namespace ui_editor
