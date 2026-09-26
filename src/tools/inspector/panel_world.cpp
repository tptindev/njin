#include "panels.h"
#include "util.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace inspector {
namespace {
// Frames the whole world: entities, level bounds and the game camera.
void fit_view(app &a, ImVec2 canvas) {
  float lo_x = 1e30f, lo_y = 1e30f, hi_x = -1e30f, hi_y = -1e30f;
  const auto add = [&](float x, float y) {
    lo_x = std::min(lo_x, x);
    lo_y = std::min(lo_y, y);
    hi_x = std::max(hi_x, x);
    hi_y = std::max(hi_y, y);
  };
  for (const entity_row &r : a.ents)
    if (r.has_pos)
      add(r.x, r.y);
  for (const tilemap_row &m : a.maps) {
    add(m.x, m.y);
    add(m.x + m.w, m.y + m.h);
  }
  if (a.has_cam) {
    add(a.cam[0], a.cam[1]);
    add(a.cam[0] + a.cam[2], a.cam[1] + a.cam[3]);
  }
  if (hi_x < lo_x)
    return;
  a.view_x = (lo_x + hi_x) * 0.5f;
  a.view_y = (lo_y + hi_y) * 0.5f;
  const float w = std::max(hi_x - lo_x, 64.0f), h = std::max(hi_y - lo_y, 64.0f);
  a.zoom = std::clamp(0.9f * std::min(canvas.x / w, canvas.y / h), 0.02f, 20.0f);
}
} // namespace

void world_window(app &a) {
  if (!begin_panel(a, "World"))
    return;
  if (ImGui::Button("Fit"))
    a.fitted = false;
  ImGui::SameLine();
  ImGui::Checkbox("follow game camera", &a.follow_game_camera);
  ImGui::SameLine();
  ImGui::TextDisabled("drag: pan (right/middle)  wheel: zoom  click: select");

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  if (size.x < 50 || size.y < 50) {
    ImGui::End();
    return;
  }
  ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  ImGuiIO &io = ImGui::GetIO();

  if (!a.fitted && (!a.ents.empty() || a.has_cam)) {
    fit_view(a, size);
    a.fitted = true;
  }
  if (a.follow_game_camera && a.has_cam) {
    a.view_x = a.cam[0] + a.cam[2] * 0.5f;
    a.view_y = a.cam[1] + a.cam[3] * 0.5f;
  }
  if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
    a.follow_game_camera = false;
    a.view_x -= io.MouseDelta.x / a.zoom;
    a.view_y -= io.MouseDelta.y / a.zoom;
  }
  const ImVec2 center{p0.x + size.x * 0.5f, p0.y + size.y * 0.5f};
  const auto to_screen = [&](float x, float y) {
    return ImVec2{center.x + (x - a.view_x) * a.zoom, center.y + (y - a.view_y) * a.zoom};
  };
  if (hovered && io.MouseWheel != 0.0f) {
    // Zoom about the mouse: the world point under it stays under it.
    const float wx = a.view_x + (io.MousePos.x - center.x) / a.zoom;
    const float wy = a.view_y + (io.MousePos.y - center.y) / a.zoom;
    a.zoom = std::clamp(a.zoom * std::pow(1.15f, io.MouseWheel), 0.02f, 40.0f);
    if (!a.follow_game_camera) {
      a.view_x = wx - (io.MousePos.x - center.x) / a.zoom;
      a.view_y = wy - (io.MousePos.y - center.y) / a.zoom;
    }
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->PushClipRect(p0, {p0.x + size.x, p0.y + size.y}, true);
  dl->AddRectFilled(p0, {p0.x + size.x, p0.y + size.y}, IM_COL32(20, 22, 28, 255));
  // Grid, every 64 world units (or coarser when zoomed out).
  float step = 64.0f;
  while (step * a.zoom < 24.0f)
    step *= 4.0f;
  const float left = a.view_x - size.x * 0.5f / a.zoom, top = a.view_y - size.y * 0.5f / a.zoom;
  for (float x = std::floor(left / step) * step; x < left + size.x / a.zoom; x += step)
    dl->AddLine(to_screen(x, top), to_screen(x, top + size.y / a.zoom), IM_COL32(40, 44, 54, 255));
  for (float y = std::floor(top / step) * step; y < top + size.y / a.zoom; y += step)
    dl->AddLine(to_screen(left, y), to_screen(left + size.x / a.zoom, y), IM_COL32(40, 44, 54, 255));

  for (const tilemap_row &m : a.maps)
    dl->AddRect(to_screen(m.x, m.y), to_screen(m.x + m.w, m.y + m.h),
                m.solid ? IM_COL32(200, 90, 90, 160) : IM_COL32(110, 110, 130, 140), 0, 0, 1.5f);
  if (a.has_cam)
    dl->AddRect(to_screen(a.cam[0], a.cam[1]), to_screen(a.cam[0] + a.cam[2], a.cam[1] + a.cam[3]),
                IM_COL32(80, 150, 255, 220), 0, 0, 2.0f);

  // Entities, and the one nearest the mouse.
  const entity_row *hover = nullptr;
  float best = 10.0f * 10.0f;
  for (const entity_row &r : a.ents) {
    const bool sel = (long long)r.id == a.selected;
    if (r.has_col) {
      const collider_row &c = r.col;
      const ImU32 col = !c.enabled ? IM_COL32(130, 130, 130, 160)
                        : c.trigger ? IM_COL32(250, 210, 60, 220)
                                    : IM_COL32(80, 230, 110, 220);
      const float t = sel ? 3.0f : 1.2f;
      if (c.circle) {
        dl->AddCircle(to_screen(c.x + c.w * 0.5f, c.y + c.h * 0.5f), c.w * 0.5f * a.zoom, sel ? IM_COL32_WHITE : col, 0, t);
      } else {
        dl->AddRect(to_screen(c.x, c.y), to_screen(c.x + c.w, c.y + c.h), sel ? IM_COL32_WHITE : col, 0, 0, t);
      }
    }
    if (r.has_pos) {
      const ImVec2 s = to_screen(r.x, r.y);
      dl->AddCircleFilled(s, sel ? 4.0f : 2.5f, sel ? IM_COL32_WHITE : IM_COL32(200, 205, 220, 200));
      const float dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
      if (hovered && dx * dx + dy * dy < best) {
        best = dx * dx + dy * dy;
        hover = &r;
      }
    }
  }
  if (hover != nullptr) {
    const ImVec2 s = to_screen(hover->x, hover->y);
    dl->AddCircle(s, 7.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
    ImGui::SetTooltip("%s  (entity %u)\n%s", hover->label.c_str(), hover->id & 0xFFFFFu, comps_text(a, *hover).c_str());
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
      select(a, hover->id);
  }
  if (hovered) {
    const float wx = a.view_x + (io.MousePos.x - center.x) / a.zoom;
    const float wy = a.view_y + (io.MousePos.y - center.y) / a.zoom;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0f, %.0f   zoom %.2f", wx, wy, a.zoom);
    dl->AddText({p0.x + 6, p0.y + size.y - 20}, IM_COL32(160, 165, 180, 255), buf);
  }
  dl->PopClipRect();
  ImGui::End();
}
} // namespace inspector
