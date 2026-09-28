#include "panels.h"
#include "preview.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui_editor {

namespace {
enum class drag_mode { none, pan, move_panel, resize_left, resize_right, widget };

struct drag_state {
  drag_mode mode = drag_mode::none;
  node_ref node;
  vec2 start_mouse{};
  vec2 start_offset{};
  float start_width = 0.0f;
};
drag_state g_drag;

// Where a dragged widget would land.
struct drop_spot {
  bool valid = false;
  int panel = -1;     // existing panel, or -1 for a new panel at `at`
  int index = 0;      // insertion index in the panel's widgets
  ImVec2 a{}, b{};    // indicator line, screen space
  vec2 at{};          // design position for a new panel
};

bool contains(const rect &r, vec2 p) {
  return p.x >= r.pos.x && p.x <= r.pos.x + r.size.x && p.y >= r.pos.y && p.y <= r.pos.y + r.size.y;
}

float snapped(const editor_app &app, float v) {
  if (!app.snap || app.snap_size <= 0.0f || ImGui::GetIO().KeyAlt)
    return v;
  return std::round(v / app.snap_size) * app.snap_size;
}

const panel_box *box_of(const editor_app &app, const node_ref &n) {
  for (const auto &b : app.boxes) {
    if (n.kind == node_ref::popup ? b.node.kind == node_ref::popup && b.node.qi == n.qi
                                  : b.node.kind == node_ref::panel && b.node.pi == n.pi)
      return &b;
  }
  return nullptr;
}

// The node under a design-space point: the topmost panel or popup, and the
// widget in it when there is one.
node_ref hit_test(const editor_app &app, vec2 p) {
  for (auto it = app.boxes.rbegin(); it != app.boxes.rend(); ++it) {
    if (!contains(it->area, p))
      continue;
    for (const auto &w : it->widgets)
      if (w.area.size.y > 0.0f && contains(w.area, p))
        return node_ref::make_widget(it->node.pi, w.index);
    return it->node;
  }
  return node_ref::make_root();
}

struct view_xf {
  ImVec2 center;
  float zoom;
  ImVec2 pan;
  ImVec2 to_screen(vec2 d) const {
    return {center.x + (d.x - pan.x) * zoom, center.y + (d.y - pan.y) * zoom};
  }
  vec2 to_design(ImVec2 s) const { return {pan.x + (s.x - center.x) / zoom, pan.y + (s.y - center.y) / zoom}; }
  ImVec2 min_of(const rect &r) const { return to_screen(r.pos); }
  ImVec2 max_of(const rect &r) const { return to_screen(r.pos + r.size); }
};

drop_spot find_drop(const editor_app &app, const view_xf &xf, vec2 m) {
  drop_spot d;
  d.valid = true;
  d.at = m;
  for (auto it = app.boxes.rbegin(); it != app.boxes.rend(); ++it) {
    if (it->node.kind != node_ref::panel || !contains(it->area, m))
      continue;
    d.panel = it->node.pi;
    const rect &pa = it->area;
    int count = (int)it->widgets.size();
    d.index = count;
    // Past the last visible widget, or in an empty panel, the line goes under it.
    float y_after = pa.pos.y + pa.size.y * 0.5f;
    for (const auto &w : it->widgets) {
      if (w.area.size.y <= 0.0f)
        continue;
      const float cy = w.area.pos.y + w.area.size.y * 0.5f;
      const bool same_line = m.y >= w.area.pos.y && m.y <= w.area.pos.y + w.area.size.y;
      const bool in_row = w.area.size.x < pa.size.x * 0.9f; // one column of a row
      if (same_line && in_row && m.x < w.area.pos.x + w.area.size.x * 0.5f) {
        d.index = w.index;
        const ImVec2 a = xf.to_screen({w.area.pos.x - 2.0f, w.area.pos.y});
        d.a = a;
        d.b = xf.to_screen({w.area.pos.x - 2.0f, w.area.pos.y + w.area.size.y});
        return d;
      }
      if (m.y < cy && !(same_line && in_row)) {
        d.index = w.index;
        d.a = xf.to_screen({w.area.pos.x, w.area.pos.y - 2.0f});
        d.b = xf.to_screen({w.area.pos.x + (in_row ? pa.size.x - (w.area.pos.x - pa.pos.x) * 2.0f : w.area.size.x),
                            w.area.pos.y - 2.0f});
        return d;
      }
      y_after = w.area.pos.y + w.area.size.y + 2.0f;
    }
    const float pad = std::min(16.0f, pa.size.x * 0.1f);
    d.a = xf.to_screen({pa.pos.x + pad, y_after});
    d.b = xf.to_screen({pa.pos.x + pa.size.x - pad, y_after});
    return d;
  }
  d.panel = -1;
  return d;
}

void toolbar(editor_app &app) {
  if (app.play_mode) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.22f, 0.22f, 1.0f));
    if (ImGui::Button("■ Dừng (F5)"))
      app.leave_play();
    ImGui::PopStyleColor();
  } else {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.3f, 1.0f));
    if (ImGui::Button("▶ Chạy thử (F5)"))
      app.enter_play();
    ImGui::PopStyleColor();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(app.play_mode);
  char res_label[48];
  std::snprintf(res_label, sizeof res_label, "%.0f x %.0f", app.layout.design_resolution.x,
                app.layout.design_resolution.y);
  ImGui::SetNextItemWidth(120);
  if (ImGui::BeginCombo("##res", res_label)) {
    for (const auto &r : k_res_presets) {
      const bool is = r.size.x == app.layout.design_resolution.x && r.size.y == app.layout.design_resolution.y;
      if (ImGui::Selectable(r.name, is)) {
        app.layout.design_resolution = r.size;
        app.fitted = false;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SetItemTooltip("Độ phân giải thiết kế (design_resolution)");
  ImGui::EndDisabled();

  if (!app.layout.custom_style) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##pstyle", &app.preview_style, "Style: Mặc định\0Style: Pixel\0");
    ImGui::SetItemTooltip("Layout kế thừa style của game: xem trước với style nào");
  }

  ImGui::SameLine();
  if (ImGui::Button("Vừa khung"))
    app.fitted = false;
  ImGui::SameLine();
  ImGui::Text("%.0f%%", app.zoom * 100.0f);
  ImGui::SameLine();
  ImGui::Checkbox("Lưới", &app.show_grid);
  ImGui::SameLine();
  ImGui::Checkbox("Neo", &app.show_anchors);
  ImGui::SameLine();
  ImGui::Checkbox("Hít", &app.snap);
  ImGui::SetItemTooltip("Hít lưới khi kéo panel (giữ Alt để tạm tắt)");
  if (app.snap) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    ImGui::DragFloat("##snap", &app.snap_size, 0.25f, 1.0f, 64.0f, "%.0f px");
  }
}

void canvas_context_menu(editor_app &app, vec2 at, const node_ref &hit) {
  if (!ImGui::BeginPopup("canvas_ctx"))
    return;
  static vec2 s_at;
  static node_ref s_hit;
  if (ImGui::IsWindowAppearing()) {
    s_at = at;
    s_hit = hit;
  }
  const node_ref target = s_hit;
  if (ImGui::BeginMenu("Thêm node tại đây")) {
    if (ImGui::MenuItem("Panel")) {
      const vec2 p = s_at;
      app.defer([&app, p] { app.sel = app.add_panel(p); });
    }
    if (ImGui::MenuItem("Popup"))
      app.defer([&app] { app.sel = app.add_popup(); });
    ImGui::Separator();
    for (ui_widget_kind k : k_all_kinds) {
      ImGui::PushStyleColor(ImGuiCol_Text, kind_color(k));
      if (ImGui::MenuItem(kind_name(k))) {
        const vec2 p = s_at;
        app.defer([&app, k, target, p] {
          if (target.kind == node_ref::panel || target.kind == node_ref::widget)
            app.sel = app.add_new((int)k, target);
          else
            app.sel = app.add_widget(app.add_panel(p).pi, 0, k);
        });
      }
      ImGui::PopStyleColor();
    }
    ImGui::EndMenu();
  }
  const bool editable = target.kind == node_ref::panel || target.kind == node_ref::widget ||
                        target.kind == node_ref::popup;
  if (ImGui::MenuItem("Nhân đôi", "Ctrl+D", false, editable))
    app.defer([&app, target] { app.sel = app.duplicate(target); });
  if (ImGui::MenuItem("Xóa", "Del", false, editable))
    app.defer([&app, target] { app.remove(target); });
  ImGui::EndPopup();
}
} // namespace

void viewport_window(editor_app &app) {
  app.view_hovered = false;
  app.view_focused = false;
  if (!app.show_viewport)
    return;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
  const bool open = ImGui::Begin("Viewport###viewport", &app.show_viewport);
  ImGui::PopStyleVar();
  if (!open) {
    ImGui::End();
    return;
  }
  toolbar(app);

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.x < 50 || avail.y < 50) {
    ImGui::End();
    return;
  }
  ImGui::InvisibleButton("##canvas", avail,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  ImGuiIO &io = ImGui::GetIO();
  app.view_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

  const vec2 res = app.shown_layout().design_resolution;
  if (!app.fitted) {
    app.zoom = std::clamp(std::min((avail.x - 40.0f) / res.x, (avail.y - 40.0f) / res.y), 0.05f, 16.0f);
    app.pan = ImVec2(res.x * 0.5f, res.y * 0.5f);
    app.fitted = true;
  }
  const ImVec2 center{p0.x + avail.x * 0.5f, p0.y + avail.y * 0.5f};

  // Pan with the right or middle button, zoom around the mouse.
  if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 2.0f) ||
                 ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 2.0f))) {
    app.pan.x -= io.MouseDelta.x / app.zoom;
    app.pan.y -= io.MouseDelta.y / app.zoom;
  }
  if (hovered && io.MouseWheel != 0.0f) {
    view_xf before{center, app.zoom, app.pan};
    const vec2 w = before.to_design(io.MousePos);
    app.zoom = std::clamp(app.zoom * std::pow(1.15f, io.MouseWheel), 0.05f, 16.0f);
    app.pan.x = w.x - (io.MousePos.x - center.x) / app.zoom;
    app.pan.y = w.y - (io.MousePos.y - center.y) / app.zoom;
  }
  const view_xf xf{center, app.zoom, app.pan};
  const vec2 mouse = xf.to_design(io.MousePos);
  const ImVec2 img_min = xf.to_screen({0.0f, 0.0f});
  const ImVec2 img_max = xf.to_screen(res);
  app.view_image_min = img_min;
  app.view_hovered = hovered && contains(rect{{0.0f, 0.0f}, res}, mouse);

  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), true);
  dl->AddRectFilled(p0, ImVec2(p0.x + avail.x, p0.y + avail.y), IM_COL32(28, 30, 36, 255));
  // The game's backdrop: a checker, so a transparent panel reads as transparent.
  {
    const float cell = 16.0f;
    dl->AddRectFilled(img_min, img_max, IM_COL32(52, 56, 66, 255));
    for (float y = 0.0f; y < res.y; y += cell)
      for (float x = ((int)(y / cell) % 2) * cell; x < res.x; x += cell * 2.0f)
        dl->AddRectFilled(xf.to_screen({x, y}), xf.to_screen({std::min(x + cell, res.x), std::min(y + cell, res.y)}),
                          IM_COL32(60, 64, 76, 255));
  }
  if (const unsigned int tex = preview_texture_id())
    dl->AddImage((ImTextureID)tex, img_min, img_max, ImVec2(0, 1), ImVec2(1, 0));

  if (app.show_grid && !app.play_mode) {
    float step = std::max(app.snap_size, 4.0f);
    while (step * app.zoom < 12.0f)
      step *= 2.0f;
    for (float x = step; x < res.x; x += step)
      dl->AddLine(xf.to_screen({x, 0.0f}), xf.to_screen({x, res.y}), IM_COL32(255, 255, 255, 14));
    for (float y = step; y < res.y; y += step)
      dl->AddLine(xf.to_screen({0.0f, y}), xf.to_screen({res.x, y}), IM_COL32(255, 255, 255, 14));
  }
  dl->AddRect(img_min, img_max, app.play_mode ? IM_COL32(230, 80, 80, 255) : IM_COL32(110, 150, 230, 255), 0, 0,
              1.5f);

  if (app.play_mode) {
    g_drag.mode = drag_mode::none;
    dl->AddText(ImVec2(p0.x + 8, p0.y + 6), IM_COL32(255, 120, 120, 255),
                "▶ Đang chạy thử: chuột và phím (khi Viewport được focus) đi vào UI của engine");
    dl->PopClipRect();
    ImGui::End();
    return;
  }

  // --- edit mode ---
  const node_ref hit = hit_test(app, mouse);
  ui_panel_data *sel_panel = app.sel.kind == node_ref::panel ? app.current_panel() : nullptr;
  const panel_box *sel_box = sel_panel ? box_of(app, app.sel) : nullptr;

  // Resize handles of the selected panel: its left and right edges.
  int on_handle = 0; // -1 left, 1 right
  if (sel_box != nullptr && (hovered || active)) {
    const ImVec2 a = xf.min_of(sel_box->area), b = xf.max_of(sel_box->area);
    if (io.MousePos.y >= a.y && io.MousePos.y <= b.y) {
      if (std::abs(io.MousePos.x - a.x) <= 5.0f)
        on_handle = -1;
      else if (std::abs(io.MousePos.x - b.x) <= 5.0f)
        on_handle = 1;
    }
  }

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    g_drag = drag_state{};
    g_drag.start_mouse = mouse;
    if (on_handle != 0 && sel_panel != nullptr) {
      g_drag.mode = on_handle < 0 ? drag_mode::resize_left : drag_mode::resize_right;
      g_drag.node = app.sel;
      g_drag.start_offset = sel_panel->offset;
      g_drag.start_width = sel_panel->width > 0.0f ? sel_panel->width : sel_box->area.size.x;
    } else {
      app.select(hit);
      g_drag.node = hit;
      if (hit.kind == node_ref::panel) {
        g_drag.mode = drag_mode::move_panel;
        g_drag.start_offset = app.panel_at(hit.pi)->offset;
      } else if (hit.kind == node_ref::widget) {
        g_drag.mode = drag_mode::widget;
      }
    }
  }
  if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const vec2 delta = mouse - g_drag.start_mouse;
    ui_panel_data *p = app.panel_at(g_drag.node.pi);
    switch (g_drag.mode) {
    case drag_mode::move_panel:
      if (p != nullptr && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
        p->offset = {snapped(app, g_drag.start_offset.x + delta.x), snapped(app, g_drag.start_offset.y + delta.y)};
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
      }
      break;
    case drag_mode::resize_left:
    case drag_mode::resize_right:
      if (p != nullptr) {
        const bool right = g_drag.mode == drag_mode::resize_right;
        float w = g_drag.start_width + (right ? delta.x : -delta.x);
        w = std::max(snapped(app, w), 40.0f);
        const float grow = w - g_drag.start_width;
        p->width = w;
        // Keep the opposite edge where it was, whatever the pivot.
        p->offset.x = g_drag.start_offset.x + (right ? grow * p->pivot.x : -grow * (1.0f - p->pivot.x));
      }
      break;
    default:
      break;
    }
  }
  if (on_handle != 0 || g_drag.mode == drag_mode::resize_left || g_drag.mode == drag_mode::resize_right)
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

  // Dragging a widget is an ImGui drag and drop, so it can land in the Scene
  // tree as well as in another place of the Viewport.
  if (g_drag.mode == drag_mode::widget && ImGui::BeginDragDropSource()) {
    const node_ref n = g_drag.node;
    ImGui::SetDragDropPayload(k_payload_node, &n, sizeof n);
    ImGui::Text("%s", app.node_name(n).c_str());
    ImGui::EndDragDropSource();
  }
  // Only now: the source above must still be submitted on the release frame.
  const bool released = !ImGui::IsMouseDown(ImGuiMouseButton_Left);

  // Right click without a drag: the context menu.
  static vec2 s_ctx_at;
  static node_ref s_ctx_hit;
  if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
      ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0.0f &&
      ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).y == 0.0f) {
    s_ctx_at = mouse;
    s_ctx_hit = hit;
    app.select(hit);
    ImGui::OpenPopup("canvas_ctx");
  }
  canvas_context_menu(app, s_ctx_at, s_ctx_hit);

  // --- drop target: palette items and dragged widgets ---
  drop_spot spot;
  bool dropping = false;
  if (ImGui::BeginDragDropTargetCustom(ImRect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y)),
                                       ImGui::GetID("##canvas_drop"))) {
    const ImGuiDragDropFlags f = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
    if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload(k_payload_new, f)) {
      const int what = *(const int *)pl->Data;
      dropping = true;
      if (what == (int)new_node::panel || what == (int)new_node::popup) {
        spot.valid = true;
        spot.panel = -1;
        spot.at = mouse;
      } else {
        spot = find_drop(app, xf, mouse);
      }
      if (pl->IsDelivery()) {
        const drop_spot s = spot;
        app.defer([&app, what, s] {
          if (what == (int)new_node::popup)
            app.sel = app.add_popup();
          else if (what == (int)new_node::panel)
            app.sel = app.add_panel(s.at);
          else if (s.panel >= 0)
            app.sel = app.add_widget(s.panel, s.index, (ui_widget_kind)what);
          else
            app.sel = app.add_widget(app.add_panel(s.at).pi, 0, (ui_widget_kind)what);
        });
      }
    }
    if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload(k_payload_node, f)) {
      const node_ref n = *(const node_ref *)pl->Data;
      if (n.kind == node_ref::widget) {
        dropping = true;
        spot = find_drop(app, xf, mouse);
        if (pl->IsDelivery()) {
          const drop_spot s = spot;
          app.defer([&app, n, s] {
            if (s.panel >= 0)
              app.sel = app.move_widget(n, s.panel, s.index);
            else
              app.sel = app.move_widget(n, app.add_panel(s.at).pi, 0);
          });
        }
      }
    }
    ImGui::EndDragDropTarget();
  }

  // --- gizmos ---
  const ImU32 col_sel = IM_COL32(255, 170, 40, 255);
  const ImU32 col_hover = IM_COL32(120, 190, 255, 160);
  if (!dropping && g_drag.mode == drag_mode::none && hovered && !(hit == app.sel)) {
    if (hit.kind == node_ref::widget) {
      if (const panel_box *b = box_of(app, node_ref::make_panel(hit.pi)))
        for (const auto &w : b->widgets)
          if (w.index == hit.wi)
            dl->AddRect(xf.min_of(w.area), xf.max_of(w.area), col_hover, 0, 0, 1.0f);
    } else if (const panel_box *b = box_of(app, hit)) {
      dl->AddRect(xf.min_of(b->area), xf.max_of(b->area), col_hover, 0, 0, 1.0f);
    }
  }

  auto name_tag = [&](ImVec2 at, const std::string &text) {
    const ImVec2 sz = ImGui::CalcTextSize(text.c_str());
    dl->AddRectFilled(ImVec2(at.x, at.y - sz.y - 4), ImVec2(at.x + sz.x + 8, at.y), col_sel, 3.0f);
    dl->AddText(ImVec2(at.x + 4, at.y - sz.y - 2), IM_COL32(20, 20, 20, 255), text.c_str());
  };

  if (app.sel.kind == node_ref::widget) {
    if (const panel_box *b = box_of(app, node_ref::make_panel(app.sel.pi))) {
      dl->AddRect(xf.min_of(b->area), xf.max_of(b->area), IM_COL32(255, 170, 40, 90), 0, 0, 1.0f);
      for (const auto &w : b->widgets) {
        if (w.index != app.sel.wi)
          continue;
        rect r = w.area;
        if (r.size.y <= 0.0f) // a row marker has no height: show a line where it starts
          r.size.y = 2.0f / app.zoom;
        dl->AddRect(xf.min_of(r), xf.max_of(r), col_sel, 0, 0, 2.0f);
        name_tag(xf.min_of(r), app.node_name(app.sel));
      }
    }
  } else if (app.sel.kind == node_ref::panel || app.sel.kind == node_ref::popup) {
    if (const panel_box *b = box_of(app, app.sel)) {
      const ImVec2 a = xf.min_of(b->area), c = xf.max_of(b->area);
      dl->AddRect(a, c, col_sel, 0, 0, 2.0f);
      name_tag(a, app.node_name(app.sel));
      if (app.sel.kind == node_ref::panel) {
        const float my = (a.y + c.y) * 0.5f;
        for (float x : {a.x, c.x})
          dl->AddRectFilled(ImVec2(x - 4, my - 8), ImVec2(x + 4, my + 8), col_sel, 2.0f);
      }
    }
  }

  // Anchor (green pin on the screen) and pivot (on the panel), like Godot's.
  if (app.show_anchors && app.sel.kind == node_ref::panel) {
    ui_panel_data *p = app.current_panel();
    if (const panel_box *b = box_of(app, app.sel); p != nullptr && b != nullptr) {
      const ImVec2 anchor = xf.to_screen(res * p->anchor);
      const ImVec2 pivot = xf.to_screen(b->area.pos + b->area.size * p->pivot);
      dl->AddLine(anchor, pivot, IM_COL32(120, 230, 120, 200), 1.0f);
      dl->AddTriangleFilled(anchor, ImVec2(anchor.x - 7, anchor.y - 12), ImVec2(anchor.x + 7, anchor.y - 12),
                            IM_COL32(110, 220, 110, 255));
      dl->AddCircleFilled(pivot, 4.0f, IM_COL32(80, 220, 240, 255));
      dl->AddCircle(pivot, 6.0f, IM_COL32(20, 20, 30, 255), 0, 1.5f);
    }
  }

  // Where a drop would land.
  if (dropping && spot.valid) {
    if (spot.panel >= 0) {
      if (const panel_box *b = box_of(app, node_ref::make_panel(spot.panel)))
        dl->AddRect(xf.min_of(b->area), xf.max_of(b->area), IM_COL32(90, 170, 255, 200), 0, 0, 1.5f);
      dl->AddLine(spot.a, spot.b, IM_COL32(90, 170, 255, 255), 3.0f);
    } else {
      const float w = 200.0f, h = 80.0f;
      const ImVec2 a = xf.to_screen({spot.at.x - w * 0.5f, spot.at.y - h * 0.5f});
      const ImVec2 c = xf.to_screen({spot.at.x + w * 0.5f, spot.at.y + h * 0.5f});
      dl->AddRectFilled(a, c, IM_COL32(90, 170, 255, 40));
      dl->AddRect(a, c, IM_COL32(90, 170, 255, 220), 0, 0, 1.5f);
      dl->AddText(ImVec2(a.x + 6, a.y + 4), IM_COL32(200, 225, 255, 255), "Panel mới");
    }
  }

  // --- keys, while the Viewport has focus ---
  if (app.view_focused && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl)
      app.fitted = false;
    if (ui_panel_data *p = app.current_panel(); p != nullptr && app.sel.kind == node_ref::panel) {
      const float step = io.KeyShift ? 10.0f : 1.0f;
      if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) p->offset.x -= step;
      if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) p->offset.x += step;
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) p->offset.y -= step;
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) p->offset.y += step;
    }
  }

  if (released)
    g_drag.mode = drag_mode::none;
  dl->PopClipRect();
  ImGui::End();
}

} // namespace ui_editor
