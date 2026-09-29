// The World panel: the game's world drawn with raylib into a render texture,
// shown as an image in the panel.
//
//   2D  entities, colliders, tilemap areas and the game camera's rectangle,
//       with a free 2D camera (pan, zoom about the mouse, click to select)
//   3D  the game's last 3D pass (render3d.h): a point for every draw (and
//       every instance), the lights, the sun and the game camera's frustum,
//       with an orbit camera (right drag: orbit, middle drag: pan, wheel: zoom)
//
// Auto picks 3D while the game draws in 3D.
#include "panels.h"
#include "util.h"
#include "imgui.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace inspector {
namespace {
// Must match debug3d_kind in the engine's render3d.h.
enum item_kind {
  item_cube = 0,
  item_sphere = 1,
  item_plane = 2,
  item_cylinder = 3,
  item_model = 4,
  item_shape = 10, // + shape3d_kind: sphere, box, capsule, cylinder, torus
};

RenderTexture2D target{};

// The render texture, sized to the canvas.
bool ensure_target(int w, int h) {
  if (IsRenderTextureValid(target) && target.texture.width == w && target.texture.height == h)
    return true;
  if (IsRenderTextureValid(target))
    UnloadRenderTexture(target);
  target = LoadRenderTexture(w, h);
  return IsRenderTextureValid(target);
}

Color rgba_of(const float c[4], float alpha_scale = 1.0f) {
  return Color{(unsigned char)(std::clamp(c[0], 0.0f, 1.0f) * 255.0f),
               (unsigned char)(std::clamp(c[1], 0.0f, 1.0f) * 255.0f),
               (unsigned char)(std::clamp(c[2], 0.0f, 1.0f) * 255.0f),
               (unsigned char)(std::clamp(c[3] * alpha_scale, 0.0f, 1.0f) * 255.0f)};
}

Color darker(Color c) { return Color{(unsigned char)(c.r * 0.55f), (unsigned char)(c.g * 0.55f), (unsigned char)(c.b * 0.55f), 255}; }

// A gizmo mark over the image: a cross for a point, else its label.
void draw_mark(ImDrawList *dl, ImVec2 s, const gizmo_mark_row &m) {
  const float c4[4] = {m.color[0], m.color[1], m.color[2], 1.0f};
  const Color c = rgba_of(c4);
  const ImU32 col = IM_COL32(c.r, c.g, c.b, 255);
  if (m.text.empty()) {
    dl->AddLine({s.x - 4, s.y - 4}, {s.x + 4, s.y + 4}, col, 1.5f);
    dl->AddLine({s.x - 4, s.y + 4}, {s.x + 4, s.y - 4}, col, 1.5f);
  } else {
    dl->AddText({s.x + 1, s.y + 1}, IM_COL32(0, 0, 0, 180), m.text.c_str());
    dl->AddText(s, col, m.text.c_str());
  }
}

// ---- 2D ----

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

void world_2d(app &a, ImVec2 p0, ImVec2 size, bool hovered) {
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

  // Line widths and dot sizes are in screen pixels, whatever the zoom.
  const float px = 1.0f / a.zoom;
  Camera2D cam{};
  cam.offset = {size.x * 0.5f, size.y * 0.5f};
  cam.target = {a.view_x, a.view_y};
  cam.zoom = a.zoom;
  BeginTextureMode(target);
  ClearBackground(Color{20, 22, 28, 255});
  BeginMode2D(cam);
  // Grid, every 64 world units (or coarser when zoomed out).
  float step = 64.0f;
  while (step * a.zoom < 24.0f)
    step *= 4.0f;
  const float left = a.view_x - size.x * 0.5f * px, top = a.view_y - size.y * 0.5f * px;
  const float right = left + size.x * px, bottom = top + size.y * px;
  for (float x = std::floor(left / step) * step; x < right; x += step)
    DrawLineEx({x, top}, {x, bottom}, px, Color{40, 44, 54, 255});
  for (float y = std::floor(top / step) * step; y < bottom; y += step)
    DrawLineEx({left, y}, {right, y}, px, Color{40, 44, 54, 255});

  for (const tilemap_row &m : a.maps)
    DrawRectangleLinesEx({m.x, m.y, m.w, m.h}, 1.5f * px,
                         m.solid ? Color{200, 90, 90, 160} : Color{110, 110, 130, 140});
  if (a.has_cam)
    DrawRectangleLinesEx({a.cam[0], a.cam[1], a.cam[2], a.cam[3]}, 2.0f * px, Color{80, 150, 255, 220});

  for (const entity_row &r : a.ents) {
    const bool sel = (long long)r.id == a.selected;
    if (r.has_col) {
      const collider_row &c = r.col;
      const Color col = sel ? WHITE
                        : !c.enabled ? Color{130, 130, 130, 160}
                        : c.trigger  ? Color{250, 210, 60, 220}
                                     : Color{80, 230, 110, 220};
      const float t = (sel ? 3.0f : 1.2f) * px;
      if (c.circle) {
        const float r0 = c.w * 0.5f;
        DrawRing({c.x + r0, c.y + r0}, std::max(r0 - t, 0.0f), r0, 0.0f, 360.0f, 48, col);
      } else {
        DrawRectangleLinesEx({c.x, c.y, c.w, c.h}, t, col);
      }
    }
    if (r.has_pos)
      DrawCircleV({r.x, r.y}, (sel ? 4.0f : 2.5f) * px, sel ? WHITE : Color{200, 205, 220, 200});
  }
  for (const gizmo_line_row &l : a.gizmo_lines)
    DrawLineEx({l.a[0], l.a[1]}, {l.b[0], l.b[1]}, 1.2f * px, rgba_of(l.color));
  EndMode2D();
  EndTextureMode();

  // The image, then what ImGui draws over it: the hovered entity, coordinates.
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddImage(ImTextureID(target.texture.id), p0, {p0.x + size.x, p0.y + size.y}, {0, 1}, {1, 0});
  const auto to_screen = [&](float x, float y) {
    return ImVec2{center.x + (x - a.view_x) * a.zoom, center.y + (y - a.view_y) * a.zoom};
  };
  for (const gizmo_mark_row &m : a.gizmo_marks)
    draw_mark(dl, to_screen(m.pos[0], m.pos[1]), m);
  const entity_row *hover = nullptr;
  float best = 10.0f * 10.0f;
  if (hovered) {
    for (const entity_row &r : a.ents) {
      if (!r.has_pos)
        continue;
      const ImVec2 s = to_screen(r.x, r.y);
      const float dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
      if (dx * dx + dy * dy < best) {
        best = dx * dx + dy * dy;
        hover = &r;
      }
    }
  }
  if (hover != nullptr) {
    dl->AddCircle(to_screen(hover->x, hover->y), 7.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
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
}

// ---- 3D ----

Vector3 v3(const float *f) { return Vector3{f[0], f[1], f[2]}; }

const char *kind_name(int kind) {
  switch (kind) {
  case item_cube: return "cube";
  case item_sphere: return "sphere";
  case item_plane: return "plane";
  case item_cylinder: return "cylinder";
  case item_model: return "model";
  case item_shape + 0: return "SDF sphere";
  case item_shape + 1: return "SDF box";
  case item_shape + 2: return "SDF capsule";
  case item_shape + 3: return "SDF cylinder";
  case item_shape + 4: return "SDF torus";
  default: return "draw";
  }
}

// Frames every point: the orbit looks at the centre of their box from far
// enough to see them all.
void fit_orbit(app &a) {
  const scene3d_state &s = a.scene3d;
  Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  const auto add = [&](Vector3 p) {
    lo = Vector3Min(lo, p);
    hi = Vector3Max(hi, p);
  };
  for (const item3d_row &it : s.items)
    add(v3(it.pos));
  add(v3(s.cam));
  if (hi.x < lo.x)
    return;
  const Vector3 c = Vector3Scale(Vector3Add(lo, hi), 0.5f);
  a.orbit_target[0] = c.x;
  a.orbit_target[1] = c.y;
  a.orbit_target[2] = c.z;
  a.orbit_dist = std::max(Vector3Length(Vector3Subtract(hi, lo)) * 1.1f, 2.0f);
}

Camera3D orbit_camera(const app &a) {
  const float yaw = a.orbit_yaw * DEG2RAD, pitch = a.orbit_pitch * DEG2RAD;
  const Vector3 t = v3(a.orbit_target);
  const Vector3 offset{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
  Camera3D cam{};
  cam.target = t;
  cam.position = Vector3Add(t, Vector3Scale(offset, a.orbit_dist));
  cam.up = {0, 1, 0};
  cam.fovy = 50.0f;
  cam.projection = CAMERA_PERSPECTIVE;
  return cam;
}

// The game camera: the lines of its view frustum out to `length`.
void draw_frustum(const scene3d_state &s, float length) {
  const Vector3 eye = v3(s.cam), target = v3(s.cam + 3), up0 = v3(s.cam + 6);
  const Vector3 f = Vector3Normalize(Vector3Subtract(target, eye));
  const Vector3 r = Vector3Normalize(Vector3CrossProduct(f, up0));
  const Vector3 u = Vector3CrossProduct(r, f);
  const float h = std::tan(s.cam[9] * 0.5f * DEG2RAD) * length;
  const float w = h * s.aspect;
  const Vector3 c = Vector3Add(eye, Vector3Scale(f, length));
  Vector3 corners[4];
  const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
  for (int i = 0; i < 4; i++)
    corners[i] = Vector3Add(c, Vector3Add(Vector3Scale(r, w * sx[i]), Vector3Scale(u, h * sy[i])));
  const Color col{80, 150, 255, 255};
  for (int i = 0; i < 4; i++) {
    DrawLine3D(eye, corners[i], col);
    DrawLine3D(corners[i], corners[(i + 1) % 4], col);
  }
}

void world_3d(app &a, ImVec2 p0, ImVec2 size, bool hovered) {
  ImGuiIO &io = ImGui::GetIO();
  scene3d_state &s = a.scene3d;
  if (!a.orbit_fitted && s.on) {
    fit_orbit(a);
    a.orbit_fitted = true;
  }
  if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
    a.follow_game_camera_3d = false;
    a.orbit_yaw -= io.MouseDelta.x * 0.4f;
    a.orbit_pitch = std::clamp(a.orbit_pitch + io.MouseDelta.y * 0.4f, -89.0f, 89.0f);
  }
  const Camera3D orbit = orbit_camera(a);
  if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    // Pan in the view plane, as far as the mouse moved at the target's depth.
    a.follow_game_camera_3d = false;
    const Vector3 f = Vector3Normalize(Vector3Subtract(orbit.target, orbit.position));
    const Vector3 r = Vector3Normalize(Vector3CrossProduct(f, orbit.up));
    const Vector3 u = Vector3CrossProduct(r, f);
    const float per_px = 2.0f * a.orbit_dist * std::tan(orbit.fovy * 0.5f * DEG2RAD) / size.y;
    const Vector3 move =
        Vector3Add(Vector3Scale(r, -io.MouseDelta.x * per_px), Vector3Scale(u, io.MouseDelta.y * per_px));
    a.orbit_target[0] += move.x;
    a.orbit_target[1] += move.y;
    a.orbit_target[2] += move.z;
  }
  if (hovered && io.MouseWheel != 0.0f)
    a.orbit_dist = std::clamp(a.orbit_dist * std::pow(0.87f, io.MouseWheel), 0.2f, 1e5f);

  // "Follow game camera": look through the game's own eye.
  Camera3D cam = orbit;
  const bool through_game = a.follow_game_camera_3d && s.on;
  if (through_game) {
    cam.position = v3(s.cam);
    cam.target = v3(s.cam + 3);
    cam.up = v3(s.cam + 6);
    cam.fovy = s.cam[9];
  }

  const int w = (int)size.x, h = (int)size.y;
  BeginTextureMode(target);
  ClearBackground(Color{20, 22, 28, 255});
  // Keep the far plane past the scene, whatever its units (the fps sample's
  // floor is 2000 wide).
  rlSetClipPlanes(0.05, std::max(2000.0, (double)a.orbit_dist * 20.0));
  BeginMode3D(cam);
  // Grid scaled to the orbit distance: always a readable number of lines.
  float spacing = 1.0f;
  while (spacing * 40.0f < a.orbit_dist)
    spacing *= 10.0f;
  rlPushMatrix();
  rlScalef(spacing, 1.0f, spacing);
  DrawGrid(40, 1.0f);
  rlPopMatrix();
  if (!through_game) {
    draw_frustum(s, std::max(a.orbit_dist * 0.15f, 0.5f));
    // Sun: a line falling onto the orbit target.
    const Vector3 t = v3(a.orbit_target);
    DrawLine3D(Vector3Subtract(t, Vector3Scale(Vector3Normalize(v3(s.sun)), a.orbit_dist * 0.25f)), t, YELLOW);
  }
  for (const gizmo_line_row &l : s.gizmo_lines)
    DrawLine3D(v3(l.a), v3(l.b), rgba_of(l.color));
  EndMode3D();

  // Every draw and light as a point of a fixed size on screen, in its colour.
  const auto on_screen = [&](const float *p, Vector2 &out) {
    const Vector3 d = Vector3Subtract(v3(p), cam.position);
    if (Vector3DotProduct(d, Vector3Subtract(cam.target, cam.position)) <= 0.0f)
      return false; // behind the camera
    out = GetWorldToScreenEx(v3(p), cam, w, h);
    return true;
  };
  Vector2 sp{};
  for (const item3d_row &it : s.items) {
    if (!on_screen(it.pos, sp))
      continue;
    Color c = rgba_of(it.color);
    c.a = 255;
    DrawCircleV(sp, 3.5f, darker(c));
    DrawCircleV(sp, 2.5f, c);
  }
  for (const light3d_row &l : s.lights) {
    if (!on_screen(l.pos, sp))
      continue;
    const float c4[4] = {l.color[0], l.color[1], l.color[2], 1.0f};
    DrawCircleLinesV(sp, 6.0f, rgba_of(c4));
    DrawCircleV(sp, 2.0f, rgba_of(c4));
  }
  if (!through_game && on_screen(s.cam, sp))
    DrawCircleV(sp, 4.0f, Color{80, 150, 255, 255});
  EndTextureMode();

  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddImage(ImTextureID(target.texture.id), p0, {p0.x + size.x, p0.y + size.y}, {0, 1}, {1, 0});
  for (const gizmo_mark_row &m : s.gizmo_marks) {
    if (on_screen(m.pos, sp))
      draw_mark(dl, {p0.x + sp.x, p0.y + sp.y}, m);
  }
  // The point under the mouse.
  if (hovered) {
    const item3d_row *hover = nullptr;
    Vector2 hover_at{};
    float best = 8.0f * 8.0f;
    for (const item3d_row &it : s.items) {
      if (!on_screen(it.pos, sp))
        continue;
      const float dx = p0.x + sp.x - io.MousePos.x, dy = p0.y + sp.y - io.MousePos.y;
      if (dx * dx + dy * dy < best) {
        best = dx * dx + dy * dy;
        hover = &it;
        hover_at = sp;
      }
    }
    if (hover != nullptr) {
      dl->AddCircle({p0.x + hover_at.x, p0.y + hover_at.y}, 7.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
      ImGui::SetTooltip("%s\n%.2f, %.2f, %.2f", kind_name(hover->kind), hover->pos[0], hover->pos[1], hover->pos[2]);
    }
  }
  char buf[128];
  std::snprintf(buf, sizeof buf, "%zu points (%lld instanced), %zu lights   distance %.1f", s.items.size(),
                s.instanced, s.lights.size(), a.orbit_dist);
  dl->AddText({p0.x + 6, p0.y + size.y - 20}, IM_COL32(160, 165, 180, 255), buf);
}
} // namespace

void world_window(app &a) {
  if (!begin_panel(a, "World"))
    return;
  const bool three_d = a.world_mode == 2 || (a.world_mode == 0 && a.scene3d.on);
  if (ImGui::Button("Fit")) {
    a.fitted = false;
    a.orbit_fitted = false;
  }
  ImGui::SameLine();
  ImGui::Checkbox("follow game camera", three_d ? &a.follow_game_camera_3d : &a.follow_game_camera);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  const char *modes[] = {"auto", "2D", "3D"};
  ImGui::Combo("##mode", &a.world_mode, modes, 3);
  ImGui::SameLine();
  if (three_d)
    ImGui::TextDisabled("right drag: orbit  middle drag: pan  wheel: zoom");
  else
    ImGui::TextDisabled("drag: pan (right/middle)  wheel: zoom  click: select");

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  if (size.x < 50 || size.y < 50 || !ensure_target((int)size.x, (int)size.y)) {
    ImGui::End();
    return;
  }
  ImGui::InvisibleButton("canvas", size,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  if (three_d && !a.scene3d.on) {
    ImGui::GetWindowDrawList()->AddText({p0.x + 8, p0.y + 8}, IM_COL32(160, 165, 180, 255),
                                        "The game has not drawn in 3D lately (begin_3d/end_3d).");
  } else if (three_d) {
    world_3d(a, p0, size, hovered);
  } else {
    world_2d(a, p0, size, hovered);
  }
  ImGui::End();
}
} // namespace inspector
