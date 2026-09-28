#include "panels.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui_editor {

namespace {
struct point9 {
  const char *tip;
  vec2 v;
};
constexpr point9 k_points[9] = {
    {"Trên trái", {0.0f, 0.0f}}, {"Trên giữa", {0.5f, 0.0f}}, {"Trên phải", {1.0f, 0.0f}},
    {"Giữa trái", {0.0f, 0.5f}}, {"Chính giữa", {0.5f, 0.5f}}, {"Giữa phải", {1.0f, 0.5f}},
    {"Dưới trái", {0.0f, 1.0f}}, {"Dưới giữa", {0.5f, 1.0f}}, {"Dưới phải", {1.0f, 1.0f}},
};

// A 3x3 grid of small squares, like Godot's anchor presets. Returns the point
// picked this frame, or -1.
int grid9(const char *id, vec2 current) {
  int picked = -1;
  ImGui::PushID(id);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3, 3));
  for (int i = 0; i < 9; ++i) {
    if (i % 3 != 0)
      ImGui::SameLine();
    const bool on = std::abs(current.x - k_points[i].v.x) < 0.001f && std::abs(current.y - k_points[i].v.y) < 0.001f;
    ImGui::PushID(i);
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.26f, 0.56f, 0.98f, 1.0f) : ImVec4(0.2f, 0.22f, 0.28f, 1.0f));
    if (ImGui::Button("##p", ImVec2(18, 18)))
      picked = i;
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", k_points[i].tip);
    ImGui::PopID();
  }
  ImGui::PopStyleVar();
  ImGui::PopID();
  return picked;
}

// A colour that is either the style's (alpha 0 in the file) or the widget's own.
void color_or_style(const char *label, njin::rgba &color, njin::rgba style_color) {
  ImGui::PushID(label);
  bool own = color.a > 0.0f;
  if (ImGui::Checkbox("##own", &own))
    color = own ? (style_color.a > 0.0f ? style_color : njin::rgba{1.0f, 1.0f, 1.0f, 1.0f}) : njin::rgba{0.0f, 0.0f, 0.0f, 0.0f};
  ImGui::SetItemTooltip("Bật: màu riêng. Tắt: lấy từ style");
  ImGui::SameLine();
  if (own)
    ImGui::ColorEdit4(label, &color.r, ImGuiColorEditFlags_AlphaBar);
  else
    ImGui::TextDisabled("%s: theo style", label);
  ImGui::PopID();
}

void root_inspector(editor_app &app) {
  ImGui::SeparatorText("Layout");
  ImGui::DragFloat2("Độ phân giải", &app.layout.design_resolution.x, 1.0f, 64.0f, 7680.0f, "%.0f");
  ImGui::SetItemTooltip("design_resolution: kích thước màn hình ảo khi thiết kế");
  if (ImGui::IsItemDeactivatedAfterEdit())
    app.fitted = false;
  ImGui::Text("Panels: %zu   Popups: %zu", app.layout.panels.size(), app.layout.popups.size());
  ImGui::TextDisabled("Chọn một node trong Scene hoặc Viewport để sửa.");
}

void panel_inspector(editor_app &app, ui_panel_data &p) {
  ImGui::SeparatorText("Panel");
  input_string("ID", p.id);
  input_string("Tiêu đề", p.title);
  ImGui::Checkbox("Hiện (visible)", &p.visible);
  ImGui::SameLine();
  ImGui::Checkbox("Nền", &p.background);

  ImGui::SeparatorText("Kích thước");
  bool auto_w = p.width <= 0.0f;
  if (ImGui::Checkbox("Rộng theo style", &auto_w))
    p.width = auto_w ? 0.0f : (app.layout.custom_style ? app.layout.style.width : 380.0f);
  if (!auto_w)
    ImGui::DragFloat("Rộng", &p.width, 1.0f, 40.0f, 4000.0f, "%.0f px");
  ImGui::TextDisabled("Chiều cao theo nội dung. Kéo mép trái/phải trong Viewport để đổi rộng.");

  ImGui::SeparatorText("Vị trí");
  ImGui::TextUnformatted("Preset (neo + tâm)");
  ImGui::SameLine(150);
  ImGui::BeginGroup();
  if (int i = grid9("preset", p.anchor); i >= 0) {
    p.anchor = p.pivot = k_points[i].v;
    // Keep off the screen's edge by a margin, as Godot's presets do.
    const float m = 16.0f;
    p.offset = {k_points[i].v.x == 0.0f ? m : k_points[i].v.x == 1.0f ? -m : 0.0f,
                k_points[i].v.y == 0.0f ? m : k_points[i].v.y == 1.0f ? -m : 0.0f};
  }
  ImGui::EndGroup();

  if (ImGui::TreeNodeEx("Neo & tâm riêng", ImGuiTreeNodeFlags_None)) {
    ImGui::TextUnformatted("Neo (anchor)");
    ImGui::SameLine(150);
    ImGui::BeginGroup();
    if (int i = grid9("anchor", p.anchor); i >= 0)
      p.anchor = k_points[i].v;
    ImGui::EndGroup();
    ImGui::SliderFloat2("anchor", &p.anchor.x, 0.0f, 1.0f, "%.2f");
    ImGui::TextUnformatted("Tâm (pivot)");
    ImGui::SameLine(150);
    ImGui::BeginGroup();
    if (int i = grid9("pivot", p.pivot); i >= 0)
      p.pivot = k_points[i].v;
    ImGui::EndGroup();
    ImGui::SliderFloat2("pivot", &p.pivot.x, 0.0f, 1.0f, "%.2f");
    ImGui::TreePop();
  }
  ImGui::DragFloat2("Offset", &p.offset.x, 1.0f, -8000.0f, 8000.0f, "%.0f px");
  ImGui::TextDisabled("Kéo panel trong Viewport; mũi tên dịch 1 px (Shift: 10 px).");
}

void widget_inspector(editor_app &app, ui_widget_data &w) {
  ImGui::SeparatorText("Widget");
  if (ImGui::BeginCombo("Loại", kind_name(w.kind))) {
    for (ui_widget_kind k : k_all_kinds) {
      ImGui::PushStyleColor(ImGuiCol_Text, kind_color(k));
      if (ImGui::Selectable(kind_name(k), k == w.kind))
        app.set_widget_kind(w, k);
      ImGui::PopStyleColor();
    }
    ImGui::EndCombo();
  }
  const bool has_label = w.kind != ui_widget_kind::space && w.kind != ui_widget_kind::row &&
                         w.kind != ui_widget_kind::progress && w.kind != ui_widget_kind::image &&
                         w.kind != ui_widget_kind::circle;
  if (w.kind != ui_widget_kind::space && w.kind != ui_widget_kind::row) {
    input_string("ID", w.id);
    ImGui::SetItemTooltip("Tên để game bắt sự kiện: ui_layout_event::widget_id");
  }
  if (has_label) {
    input_string(w.kind == ui_widget_kind::label ? "Chữ" : "Nhãn", w.label);
    ImGui::SetItemTooltip("'Chữ##id' để hai widget cùng nhãn vẫn khác nhau");
  }
  if (w.kind == ui_widget_kind::button)
    ImGui::Checkbox("Bật (enabled)", &w.enabled);

  switch (w.kind) {
  case ui_widget_kind::space:
    ImGui::DragFloat("Cao", &w.height, 0.5f, 0.0f, 400.0f, "%.0f px");
    break;
  case ui_widget_kind::row:
    ImGui::SliderInt("Số cột", &w.columns, 1, 6);
    ImGui::TextDisabled("Xếp %d widget tiếp theo thành một hàng", w.columns);
    break;
  case ui_widget_kind::toggle:
    ImGui::Checkbox("Giá trị", &w.bool_val);
    break;
  case ui_widget_kind::slider:
    ImGui::DragFloat("Giá trị", &w.float_val, 0.01f, w.min_val, w.max_val);
    ImGui::DragFloat("Min", &w.min_val, 0.1f);
    ImGui::DragFloat("Max", &w.max_val, 0.1f);
    ImGui::DragFloat("Bước", &w.step, 0.01f, 0.0f, 1000.0f, "%.3f");
    ImGui::Checkbox("Hiện dạng %", &w.percent);
    break;
  case ui_widget_kind::choice: {
    ImGui::SliderInt("Mục chọn", &w.int_val, 0, std::max(0, (int)w.options.size() - 1));
    ImGui::SeparatorText("Các mục");
    int remove = -1;
    for (size_t i = 0; i < w.options.size(); ++i) {
      ImGui::PushID((int)i);
      ImGui::SetNextItemWidth(-40);
      input_string("##opt", w.options[i]);
      ImGui::SameLine();
      if (w.options.size() > 1 && ImGui::SmallButton("x"))
        remove = (int)i;
      ImGui::PopID();
    }
    if (remove >= 0) {
      w.options.erase(w.options.begin() + remove);
      w.int_val = std::min(w.int_val, (int)w.options.size() - 1);
    }
    if (ImGui::Button("+ Thêm mục"))
      w.options.push_back("Mục mới");
    break;
  }
  case ui_widget_kind::progress:
    ImGui::SliderFloat("Giá trị", &w.float_val, 0.0f, 1.0f, "%.2f");
    input_string("Chữ", w.text);
    break;
  case ui_widget_kind::circle:
    ImGui::SliderFloat("Giá trị", &w.float_val, 0.0f, 1.0f, "%.2f");
    ImGui::SeparatorText("Kiểu dáng");
    ImGui::DragFloat("Đường kính", &w.diameter, 0.5f, 8.0f, 1000.0f, "%.0f px");
    ImGui::DragFloat("Độ dày", &w.thickness, 0.25f, 1.0f, 500.0f, "%.0f px");
    ImGui::SetItemTooltip("Từ nửa đường kính trở lên thì thành hình tròn đặc");
    ImGui::DragFloat("Góc bắt đầu", &w.start_angle, 1.0f, -360.0f, 360.0f, "%.0f°");
    ImGui::SetItemTooltip("0 là đỉnh (12 giờ), 90 là 3 giờ; tính theo chiều kim đồng hồ");
    ImGui::Checkbox("Theo chiều kim đồng hồ", &w.clockwise);
    ImGui::Checkbox("Đầu bo tròn", &w.round_caps);
    ImGui::Checkbox("Vẽ vòng nền", &w.show_track);
    ImGui::SeparatorText("Màu");
    color_or_style("Màu phần đầy", w.fill_color, app.layout.style.fill.normal.color);
    color_or_style("Màu vòng nền", w.track_color, app.layout.style.track.normal.color);
    ImGui::SeparatorText("Chữ giữa vòng");
    input_string("Chữ", w.text);
    ImGui::BeginDisabled(!w.text.empty());
    ImGui::Checkbox("Hiện phần trăm", &w.percent);
    ImGui::EndDisabled();
    break;
  case ui_widget_kind::image:
    input_string("Ảnh", w.texture_path);
    ImGui::SetItemTooltip("Đường dẫn file ảnh (tương đối với thư mục chạy hoặc file layout)");
    ImGui::DragFloat2("Kích thước", &w.size.x, 1.0f, 1.0f, 4000.0f, "%.0f px");
    if (!w.texture_path.empty() && app.textures.count(w.texture_path) && app.textures[w.texture_path].id == 0)
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Không nạp được ảnh này");
    ImGui::TextDisabled("Lưu ý: ui_layout_load không nạp ảnh, game phải tự gán texture.");
    break;
  case ui_widget_kind::keybind:
    input_string("Action", w.action_name);
    ImGui::Checkbox("Tay cầm", &w.pad);
    ImGui::TextDisabled("ui_draw_panel vẽ keybind như một label.");
    break;
  default:
    break;
  }
}

void popup_inspector(ui_popup_data &pop) {
  ImGui::SeparatorText("Popup");
  input_string("ID", pop.id);
  input_string("Tiêu đề", pop.title);
  input_string_multiline("Nội dung", pop.message, ImVec2(-1, 70));
  ImGui::DragFloat("Rộng", &pop.width, 1.0f, 0.0f, 4000.0f, "%.0f px (0: theo style)");
  ImGui::Checkbox("Đang mở (open)", &pop.open);

  ImGui::SeparatorText("Nút (tối đa 4)");
  int remove = -1;
  for (size_t i = 0; i < pop.buttons.size(); ++i) {
    ImGui::PushID((int)i);
    ImGui::SetNextItemWidth(-40);
    input_string("##btn", pop.buttons[i]);
    ImGui::SameLine();
    if (pop.buttons.size() > 1 && ImGui::SmallButton("x"))
      remove = (int)i;
    ImGui::PopID();
  }
  if (remove >= 0)
    pop.buttons.erase(pop.buttons.begin() + remove);
  if (pop.buttons.size() < 4 && ImGui::Button("+ Thêm nút"))
    pop.buttons.push_back("Nút");
  const int last = (int)pop.buttons.size() - 1;
  ImGui::SliderInt("Nút mặc định", &pop.default_button, 0, last);
  ImGui::SliderInt("Nút khi Back", &pop.cancel_button, -1, last, pop.cancel_button < 0 ? "không" : "%d");
}
} // namespace

void inspector_window(editor_app &app) {
  if (!app.show_inspector)
    return;
  if (!ImGui::Begin("Inspector###inspector", &app.show_inspector)) {
    ImGui::End();
    return;
  }
  if (app.play_mode) {
    ImGui::TextDisabled("Đang chạy thử: dừng (F5) để sửa.");
    ImGui::End();
    return;
  }
  ImGui::PushItemWidth(-110);
  switch (app.sel.kind) {
  case node_ref::panel:
    if (auto *p = app.current_panel())
      panel_inspector(app, *p);
    break;
  case node_ref::widget:
    if (auto *w = app.current_widget()) {
      ImGui::TextDisabled("trong panel %s", app.layout.panels[(size_t)app.sel.pi].id.c_str());
      widget_inspector(app, *w);
    }
    break;
  case node_ref::popup:
    if (auto *pop = app.current_popup())
      popup_inspector(*pop);
    break;
  default:
    root_inspector(app);
    break;
  }
  ImGui::PopItemWidth();
  ImGui::End();
}

} // namespace ui_editor
