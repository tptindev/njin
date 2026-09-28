#include "panels.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace ui_editor {

namespace {
const char *k_panel_desc = "Khung chứa các widget xếp dọc (ui_begin/ui_end)";
const char *k_popup_desc = "Hộp thoại modal với tối đa 4 nút (ui_popup)";

const char *kind_desc(ui_widget_kind k) {
  switch (k) {
  case ui_widget_kind::label: return "Dòng chữ (ui_label)";
  case ui_widget_kind::space: return "Khoảng trống dọc (ui_space)";
  case ui_widget_kind::button: return "Nút bấm (ui_button)";
  case ui_widget_kind::toggle: return "Công tắc bật/tắt (ui_toggle)";
  case ui_widget_kind::slider: return "Thanh trượt giá trị (ui_slider)";
  case ui_widget_kind::choice: return "Chọn một trong nhiều mục (ui_choice)";
  case ui_widget_kind::progress: return "Thanh tiến độ (ui_progress)";
  case ui_widget_kind::image: return "Ảnh tĩnh (ui_image)";
  case ui_widget_kind::row: return "Xếp N widget tiếp theo thành một hàng ngang (ui_row)";
  case ui_widget_kind::keybind: return "Dòng gán phím (ui_keybind)";
  case ui_widget_kind::circle: return "Thanh tiến độ hình tròn (ui_progress_circle)";
  }
  return "";
}

// Where a node dropped on a tree item goes, from the mouse's height in it.
enum class drop_zone { before, into, after };
drop_zone zone_of(ImVec2 min, ImVec2 max, bool can_before_after, bool can_into) {
  const float t = (ImGui::GetIO().MousePos.y - min.y) / std::max(1.0f, max.y - min.y);
  if (!can_before_after)
    return drop_zone::into;
  if (!can_into)
    return t < 0.5f ? drop_zone::before : drop_zone::after;
  return t < 0.25f ? drop_zone::before : t > 0.75f ? drop_zone::after : drop_zone::into;
}

void draw_zone(ImVec2 min, ImVec2 max, drop_zone z) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImU32 col = IM_COL32(90, 170, 255, 255);
  switch (z) {
  case drop_zone::before: dl->AddLine(ImVec2(min.x, min.y), ImVec2(max.x, min.y), col, 2.0f); break;
  case drop_zone::after: dl->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), col, 2.0f); break;
  case drop_zone::into: dl->AddRect(min, max, col, 2.0f, 0, 1.5f); break;
  }
}

// A tree item as a drop target, for new nodes from the palette and for nodes
// dragged within the tree or from the Viewport.
void drop_target(editor_app &app, const node_ref &target) {
  if (!ImGui::BeginDragDropTarget())
    return;
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax(); // tree rows span the width
  const ImGuiDragDropFlags f = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;

  if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload(k_payload_new, f)) {
    const int what = *(const int *)pl->Data;
    const bool is_widget = what < (int)new_node::panel;
    drop_zone z = drop_zone::into;
    if (is_widget && target.kind == node_ref::widget)
      z = zone_of(min, max, true, false);
    else if (what == (int)new_node::panel && target.kind == node_ref::panel)
      z = zone_of(min, max, true, false);
    draw_zone(min, max, z);
    if (pl->IsDelivery()) {
      app.defer([&app, what, target, z, is_widget] {
        if (is_widget && target.kind == node_ref::widget)
          app.sel = app.add_widget(target.pi, target.wi + (z == drop_zone::after ? 1 : 0), (ui_widget_kind)what);
        else if (what == (int)new_node::panel && target.kind == node_ref::panel) {
          const node_ref p = app.add_panel(app.layout.design_resolution * 0.5f);
          app.sel = app.move_panel(p.pi, target.pi + (z == drop_zone::after ? 1 : 0));
        } else
          app.sel = app.add_new(what, target);
      });
    }
  } else if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload(k_payload_node, f)) {
    const node_ref n = *(const node_ref *)pl->Data;
    bool ok = false;
    drop_zone z = drop_zone::into;
    if (n.kind == node_ref::widget && target.kind == node_ref::widget && !(n == target)) {
      ok = true;
      z = zone_of(min, max, true, false);
    } else if (n.kind == node_ref::widget && target.kind == node_ref::panel) {
      ok = true;
    } else if (n.kind == node_ref::panel && target.kind == node_ref::panel && n.pi != target.pi) {
      ok = true;
      z = zone_of(min, max, true, false);
    } else if (n.kind == node_ref::popup && target.kind == node_ref::popup && n.qi != target.qi) {
      ok = true;
      z = zone_of(min, max, true, false);
    }
    if (ok) {
      draw_zone(min, max, z);
      if (pl->IsDelivery()) {
        const int after = z == drop_zone::after ? 1 : 0;
        app.defer([&app, n, target, after] {
          if (n.kind == node_ref::widget && target.kind == node_ref::widget)
            app.sel = app.move_widget(n, target.pi, target.wi + after);
          else if (n.kind == node_ref::widget)
            app.sel = app.move_widget(n, target.pi, (int)app.layout.panels[(size_t)target.pi].widgets.size());
          else if (n.kind == node_ref::panel)
            app.sel = app.move_panel(n.pi, target.pi + after);
          else
            app.sel = app.move_popup(n.qi, target.qi + after);
        });
      }
    }
  }
  ImGui::EndDragDropTarget();
}

void drag_source(editor_app &app, const node_ref &n) {
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(k_payload_node, &n, sizeof n);
    ImGui::Text("%s", app.node_name(n).c_str());
    ImGui::EndDragDropSource();
  }
}

void node_context_menu(editor_app &app, const node_ref &n) {
  if (!ImGui::BeginPopupContextItem("ctx"))
    return;
  app.select(n);
  if (ImGui::BeginMenu("Thêm node con")) {
    add_node_menu_items(app, n);
    ImGui::EndMenu();
  }
  if (n.kind == node_ref::widget) {
    if (ImGui::BeginMenu("Đổi loại")) {
      for (ui_widget_kind k : k_all_kinds) {
        if (ImGui::MenuItem(kind_name(k)))
          app.defer([&app, n, k] {
            if (auto *w = app.widget_at(n))
              app.set_widget_kind(*w, k);
          });
      }
      ImGui::EndMenu();
    }
  }
  if (n.kind != node_ref::root) {
    ImGui::Separator();
    if (ImGui::MenuItem("Nhân đôi", "Ctrl+D"))
      app.defer([&app, n] { app.sel = app.duplicate(n); });
    if (ImGui::MenuItem("Lên trên", "Ctrl+Up"))
      app.defer([&app, n] { move_selection(app, n, -1); });
    if (ImGui::MenuItem("Xuống dưới", "Ctrl+Down"))
      app.defer([&app, n] { move_selection(app, n, 1); });
    ImGui::Separator();
    if (ImGui::MenuItem("Xóa", "Del"))
      app.defer([&app, n] { app.remove(n); });
  }
  ImGui::EndPopup();
}

ImGuiTreeNodeFlags node_flags(const editor_app &app, const node_ref &n, bool leaf) {
  ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                         ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen |
                         ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
  if (leaf)
    f |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  if (app.sel == n)
    f |= ImGuiTreeNodeFlags_Selected;
  return f;
}

void click_select(editor_app &app, const node_ref &n) {
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    app.select(n);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    app.select(n);
}

// The eye on the right of a row: shown, or hidden in the game.
void eye(bool &visible, const char *id) {
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight());
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
  ImGui::PushID(id);
  ImGui::Checkbox("##eye", &visible);
  ImGui::SetItemTooltip(visible ? "Đang hiện (visible)" : "Đang ẩn");
  ImGui::PopID();
  ImGui::PopStyleVar();
}
} // namespace

void move_selection(editor_app &app, const node_ref &n, int dir) {
  switch (n.kind) {
  case node_ref::widget:
    app.sel = app.move_widget(n, n.pi, n.wi + (dir > 0 ? 2 : -1));
    break;
  case node_ref::panel:
    app.sel = app.move_panel(n.pi, n.pi + (dir > 0 ? 2 : -1));
    break;
  case node_ref::popup:
    app.sel = app.move_popup(n.qi, n.qi + (dir > 0 ? 2 : -1));
    break;
  default:
    break;
  }
}

void add_node_menu_items(editor_app &app, const node_ref &target) {
  if (ImGui::MenuItem("Panel"))
    app.defer([&app] { app.sel = app.add_new((int)new_node::panel, node_ref::make_root()); });
  ImGui::SetItemTooltip("%s", k_panel_desc);
  if (ImGui::MenuItem("Popup"))
    app.defer([&app] { app.sel = app.add_new((int)new_node::popup, node_ref::make_root()); });
  ImGui::SetItemTooltip("%s", k_popup_desc);
  ImGui::Separator();
  for (ui_widget_kind k : k_all_kinds) {
    ImGui::PushStyleColor(ImGuiCol_Text, kind_color(k));
    const bool pick = ImGui::MenuItem(kind_name(k));
    ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", kind_desc(k));
    if (pick)
      app.defer([&app, k, target] { app.sel = app.add_new((int)k, target); });
  }
}

void scene_window(editor_app &app) {
  app.scene_focused = false;
  if (!app.show_scene)
    return;
  if (!ImGui::Begin("Scene###scene", &app.show_scene)) {
    ImGui::End();
    return;
  }
  app.scene_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  ImGui::BeginDisabled(app.play_mode);

  if (ImGui::Button("+"))
    ImGui::OpenPopup("add_node");
  ImGui::SetItemTooltip("Thêm node con vào node đang chọn (Ctrl+A)");
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
      ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A))
    ImGui::OpenPopup("add_node");
  if (ImGui::BeginPopup("add_node")) {
    ImGui::TextDisabled("Thêm vào: %s", app.node_name(app.sel).c_str());
    ImGui::Separator();
    add_node_menu_items(app, app.sel);
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Kéo thả để sắp xếp; kéo từ Nodes để thêm");

  ImGui::Separator();
  ImGui::BeginChild("tree");

  // Root
  const node_ref root = node_ref::make_root();
  char root_label[64];
  std::snprintf(root_label, sizeof root_label, "Layout  %.0fx%.0f", app.layout.design_resolution.x,
                app.layout.design_resolution.y);
  const bool root_open = ImGui::TreeNodeEx("##root", node_flags(app, root, false), "%s", root_label);
  click_select(app, root);
  drop_target(app, root);
  ImGui::PushID("root");
  node_context_menu(app, root);
  ImGui::PopID();

  if (root_open) {
    for (size_t pi = 0; pi < app.layout.panels.size(); ++pi) {
      ui_panel_data &p = app.layout.panels[pi];
      const node_ref pn = node_ref::make_panel((int)pi);
      ImGui::PushID((int)pi);
      ImGui::PushStyleColor(ImGuiCol_Text, p.visible ? ImVec4(0.55f, 0.85f, 0.6f, 1.0f) : ImVec4(0.45f, 0.5f, 0.45f, 1.0f));
      const bool open = ImGui::TreeNodeEx("##panel", node_flags(app, pn, p.widgets.empty()), "▣ %s", p.id.c_str());
      ImGui::PopStyleColor();
      click_select(app, pn);
      drag_source(app, pn);
      drop_target(app, pn);
      node_context_menu(app, pn);
      eye(p.visible, "vis");

      if (open && !p.widgets.empty()) {
        int row_left = 0;
        for (size_t wi = 0; wi < p.widgets.size(); ++wi) {
          ui_widget_data &w = p.widgets[wi];
          const node_ref wn = node_ref::make_widget((int)pi, (int)wi);
          const bool in_row = row_left > 0 && w.kind != ui_widget_kind::space && w.kind != ui_widget_kind::row;
          if (in_row)
            ImGui::Indent(14.0f);
          ImGui::PushID((int)wi);
          ImGui::PushStyleColor(ImGuiCol_Text, kind_color(w.kind));
          char label[160];
          if (w.kind == ui_widget_kind::row)
            std::snprintf(label, sizeof label, "Row ×%d", w.columns);
          else if (w.kind == ui_widget_kind::space)
            std::snprintf(label, sizeof label, "Space %.0f", w.height);
          else
            std::snprintf(label, sizeof label, "%s", w.id.empty() ? kind_name(w.kind) : w.id.c_str());
          ImGui::TreeNodeEx("##w", node_flags(app, wn, true), "%s", label);
          ImGui::PopStyleColor();
          click_select(app, wn);
          drag_source(app, wn);
          drop_target(app, wn);
          node_context_menu(app, wn);
          if (w.kind != ui_widget_kind::row && w.kind != ui_widget_kind::space && !w.label.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("\"%s\"", w.label.c_str());
          }
          ImGui::PopID();
          if (in_row) {
            ImGui::Unindent(14.0f);
            row_left--;
          }
          if (w.kind == ui_widget_kind::row)
            row_left = w.columns;
        }
      }
      if (open && !p.widgets.empty())
        ImGui::TreePop();
      ImGui::PopID();
    }

    for (size_t i = 0; i < app.layout.popups.size(); ++i) {
      ui_popup_data &pop = app.layout.popups[i];
      const node_ref qn = node_ref::make_popup((int)i);
      ImGui::PushID(10000 + (int)i);
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.45f, 1.0f));
      ImGui::TreeNodeEx("##popup", node_flags(app, qn, true), "◈ %s", pop.id.c_str());
      ImGui::PopStyleColor();
      click_select(app, qn);
      drag_source(app, qn);
      drop_target(app, qn);
      node_context_menu(app, qn);
      eye(pop.open, "open");
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  // Empty space under the tree: drop to the root, click to select it.
  const ImVec2 rest = ImGui::GetContentRegionAvail();
  if (rest.y > 4.0f) {
    ImGui::InvisibleButton("##below", ImVec2(std::max(rest.x, 1.0f), rest.y));
    if (ImGui::IsItemClicked())
      app.select(root);
    drop_target(app, root);
  }
  ImGui::EndChild();

  // Keys while the tree has focus.
  ImGuiIO &io = ImGui::GetIO();
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
    const node_ref n = app.sel;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && io.KeyCtrl)
      app.defer([&app, n] { move_selection(app, n, -1); });
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && io.KeyCtrl)
      app.defer([&app, n] { move_selection(app, n, 1); });
  }

  ImGui::EndDisabled();
  ImGui::End();
}

void nodes_window(editor_app &app) {
  if (!app.show_nodes)
    return;
  if (!ImGui::Begin("Nodes###nodes", &app.show_nodes)) {
    ImGui::End();
    return;
  }
  static std::string filter;
  ImGui::SetNextItemWidth(-1);
  input_string("##filter", filter, ImGuiInputTextFlags_None);
  if (filter.empty()) {
    const ImVec2 r = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddText(ImVec2(r.x + 6, r.y + 3), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                        "Tìm node...");
  }
  ImGui::TextDisabled("Kéo vào Viewport hoặc Scene; bấm đúp để thêm");
  ImGui::Separator();

  auto matches = [&](const char *name) {
    if (filter.empty())
      return true;
    std::string a = name, b = filter;
    for (auto &c : a) c = (char)std::tolower((unsigned char)c);
    for (auto &c : b) c = (char)std::tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
  };
  auto item = [&](int what, const char *name, const char *desc, ImVec4 color) {
    if (!matches(name))
      return;
    ImGui::PushID(what);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const bool clicked = ImGui::Selectable(name, false, ImGuiSelectableFlags_AllowDoubleClick);
    ImGui::PopStyleColor();
    if (clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !app.play_mode) {
      const node_ref target = app.sel;
      app.defer([&app, what, target] { app.sel = app.add_new(what, target); });
    }
    if (ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload(k_payload_new, &what, sizeof what);
      ImGui::TextColored(color, "+ %s", name);
      ImGui::EndDragDropSource();
    }
    ImGui::SetItemTooltip("%s", desc);
    ImGui::PopID();
  };

  ImGui::BeginDisabled(app.play_mode);
  ImGui::SeparatorText("Khung chứa");
  item((int)new_node::panel, "Panel", k_panel_desc, ImVec4(0.55f, 0.85f, 0.6f, 1.0f));
  item((int)new_node::popup, "Popup", k_popup_desc, ImVec4(0.95f, 0.75f, 0.45f, 1.0f));
  item((int)ui_widget_kind::row, "Row", kind_desc(ui_widget_kind::row), kind_color(ui_widget_kind::row));
  item((int)ui_widget_kind::space, "Space", kind_desc(ui_widget_kind::space), kind_color(ui_widget_kind::space));
  ImGui::SeparatorText("Điều khiển");
  for (ui_widget_kind k : k_all_kinds) {
    if (k == ui_widget_kind::row || k == ui_widget_kind::space)
      continue;
    item((int)k, kind_name(k), kind_desc(k), kind_color(k));
  }
  ImGui::EndDisabled();
  ImGui::End();
}

} // namespace ui_editor
