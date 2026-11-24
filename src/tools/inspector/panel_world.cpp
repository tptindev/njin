// The World panel: the game's world drawn with ImGui's draw list into the panel.
//
//   2D  entities, colliders, tilemap areas and the game camera's rectangle,
//       with a free 2D camera (pan, zoom about the mouse, click to select)
//   3D  the game's last 3D pass (render3d.h): a point for every draw (and
//       every instance), the lights, the sun and the game camera's frustum,
//       with an orbit camera (right drag: orbit, middle drag: pan, wheel: zoom)
//
// Auto picks 3D while the game draws in 3D. The 3D view projects as raylib's
// GetWorldToScreenEx did (perspective, vertical fovy), and lines are cut at the
// near plane, so a line passing behind the eye still draws its visible part.
#include "panels.h"
#include "util.h"
#include "imgui.h"
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

constexpr float deg2rad = 3.14159265358979323846f / 180.0f;
constexpr ImU32 background = IM_COL32(20, 22, 28, 255);

ImU32 rgba_of(const float c[4], float alpha_scale = 1.0f) {
  return IM_COL32((int)(std::clamp(c[0], 0.0f, 1.0f) * 255.0f), (int)(std::clamp(c[1], 0.0f, 1.0f) * 255.0f),
                  (int)(std::clamp(c[2], 0.0f, 1.0f) * 255.0f),
                  (int)(std::clamp(c[3] * alpha_scale, 0.0f, 1.0f) * 255.0f));
}

ImU32 opaque(ImU32 c) { return (c & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 255); }

ImU32 darker(ImU32 c) {
  const auto ch = [&](int shift) { return (int)(((c >> shift) & 0xFF) * 0.55f); };
  return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), 255);
}

// A gizmo mark over the view: a cross for a point, else its label.
void draw_mark(ImDrawList *dl, ImVec2 s, const gizmo_mark_row &m) {
  const float c4[4] = {m.color[0], m.color[1], m.color[2], 1.0f};
  const ImU32 col = rgba_of(c4);
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
  const auto to_screen = [&](float x, float y) {
    return ImVec2{center.x + (x - a.view_x) * a.zoom, center.y + (y - a.view_y) * a.zoom};
  };

  // Line widths and dot sizes are in screen pixels, whatever the zoom.
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 p1{p0.x + size.x, p0.y + size.y};
  dl->PushClipRect(p0, p1, true);
  dl->AddRectFilled(p0, p1, background);
  // Grid, every 64 world units (or coarser when zoomed out).
  float step = 64.0f;
  while (step * a.zoom < 24.0f)
    step *= 4.0f;
  const float px = 1.0f / a.zoom;
  const float left = a.view_x - size.x * 0.5f * px, top = a.view_y - size.y * 0.5f * px;
  const float right = left + size.x * px, bottom = top + size.y * px;
  const ImU32 grid = IM_COL32(40, 44, 54, 255);
  for (float x = std::floor(left / step) * step; x < right; x += step)
    dl->AddLine(to_screen(x, top), to_screen(x, bottom), grid, 1.0f);
  for (float y = std::floor(top / step) * step; y < bottom; y += step)
    dl->AddLine(to_screen(left, y), to_screen(right, y), grid, 1.0f);

  for (const tilemap_row &m : a.maps)
    dl->AddRect(to_screen(m.x, m.y), to_screen(m.x + m.w, m.y + m.h),
                m.solid ? IM_COL32(200, 90, 90, 160) : IM_COL32(110, 110, 130, 140), 0.0f, 0, 1.5f);
  if (a.has_cam)
    dl->AddRect(to_screen(a.cam[0], a.cam[1]), to_screen(a.cam[0] + a.cam[2], a.cam[1] + a.cam[3]),
                IM_COL32(80, 150, 255, 220), 0.0f, 0, 2.0f);

  for (const entity_row &r : a.ents) {
    const bool sel = (long long)r.id == a.selected;
    if (r.has_col) {
      const collider_row &c = r.col;
      const ImU32 col = sel ? IM_COL32(255, 255, 255, 255)
                        : !c.enabled ? IM_COL32(130, 130, 130, 160)
                        : c.trigger  ? IM_COL32(250, 210, 60, 220)
                                     : IM_COL32(80, 230, 110, 220);
      const float t = sel ? 3.0f : 1.2f;
      if (c.circle) {
        const float r0 = c.w * 0.5f;
        dl->AddCircle(to_screen(c.x + r0, c.y + r0), std::max(r0 * a.zoom - t * 0.5f, 0.5f), col, 48, t);
      } else {
        dl->AddRect(to_screen(c.x, c.y), to_screen(c.x + c.w, c.y + c.h), col, 0.0f, 0, t);
      }
    }
    if (r.has_pos)
      dl->AddCircleFilled(to_screen(r.x, r.y), sel ? 4.0f : 2.5f,
                          sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 205, 220, 200));
  }
  for (const gizmo_line_row &l : a.gizmo_lines)
    dl->AddLine(to_screen(l.a[0], l.a[1]), to_screen(l.b[0], l.b[1]), rgba_of(l.color), 1.2f);

  // Over the world: gizmo marks, the hovered entity, coordinates.
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
  dl->PopClipRect();
}

// ---- 3D ----

struct v3 {
  float x = 0, y = 0, z = 0;
};
v3 vec(const float *f) { return {f[0], f[1], f[2]}; }
v3 operator+(v3 a, v3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
v3 operator-(v3 a, v3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
v3 operator*(v3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
v3 cross(v3 a, v3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(v3 a) { return std::sqrt(dot(a, a)); }
v3 normalize(v3 a) {
  const float l = length(a);
  return l > 0.0f ? a * (1.0f / l) : a;
}
v3 vmin(v3 a, v3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
v3 vmax(v3 a, v3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

struct camera3 {
  v3 position, target, up{0, 1, 0};
  float fovy = 50.0f;
};

// Projects into the panel: the camera's basis, a vertical field of view and the panel's
// aspect, as raylib's GetWorldToScreenEx (MatrixLookAt + MatrixPerspective) did.
struct projector {
  v3 eye, r, u, f;
  float sy = 1, sx = 1, near_z = 0.05f;
  ImVec2 p0, size;
  projector(const camera3 &c, ImVec2 origin, ImVec2 extent) : eye(c.position), p0(origin), size(extent) {
    f = normalize(c.target - c.position);
    r = normalize(cross(f, c.up));
    u = cross(r, f);
    sy = 1.0f / std::tan(c.fovy * 0.5f * deg2rad);
    sx = sy / (extent.x / extent.y);
  }
  // View space: x right, y up, z forward (depth).
  v3 view(v3 p) const {
    const v3 d = p - eye;
    return {dot(d, r), dot(d, u), dot(d, f)};
  }
  ImVec2 screen(v3 v) const {
    const float ndc_x = v.x * sx / v.z, ndc_y = v.y * sy / v.z;
    return {p0.x + (ndc_x + 1.0f) * 0.5f * size.x, p0.y + (1.0f - ndc_y) * 0.5f * size.y};
  }
  bool point(const float *p, ImVec2 &out) const { return point(vec(p), out); }
  bool point(v3 p, ImVec2 &out) const {
    const v3 v = view(p);
    if (v.z <= near_z)
      return false; // behind the camera
    out = screen(v);
    return true;
  }
  // A line, cut where it crosses the near plane.
  void line(ImDrawList *dl, v3 a, v3 b, ImU32 col, float thickness = 1.0f) const {
    v3 va = view(a), vb = view(b);
    if (va.z <= near_z && vb.z <= near_z)
      return;
    if (va.z <= near_z || vb.z <= near_z) {
      const float t = (near_z - va.z) / (vb.z - va.z);
      const v3 cut = va + (vb - va) * t;
      (va.z <= near_z ? va : vb) = cut;
    }
    dl->AddLine(screen(va), screen(vb), col, thickness);
  }
};

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
  v3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  const auto add = [&](v3 p) {
    lo = vmin(lo, p);
    hi = vmax(hi, p);
  };
  for (const item3d_row &it : s.items)
    add(vec(it.pos));
  add(vec(s.cam));
  if (hi.x < lo.x)
    return;
  const v3 c = (lo + hi) * 0.5f;
  a.orbit_target[0] = c.x;
  a.orbit_target[1] = c.y;
  a.orbit_target[2] = c.z;
  a.orbit_dist = std::max(length(hi - lo) * 1.1f, 2.0f);
}

camera3 orbit_camera(const app &a) {
  const float yaw = a.orbit_yaw * deg2rad, pitch = a.orbit_pitch * deg2rad;
  const v3 t = vec(a.orbit_target);
  const v3 offset{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
  camera3 cam;
  cam.target = t;
  cam.position = t + offset * a.orbit_dist;
  cam.fovy = 50.0f;
  return cam;
}

// The game camera: the lines of its view frustum out to `length`.
void draw_frustum(ImDrawList *dl, const projector &pr, const scene3d_state &s, float distance) {
  const v3 eye = vec(s.cam), target = vec(s.cam + 3), up0 = vec(s.cam + 6);
  const v3 f = normalize(target - eye);
  const v3 r = normalize(cross(f, up0));
  const v3 u = cross(r, f);
  const float h = std::tan(s.cam[9] * 0.5f * deg2rad) * distance;
  const float w = h * s.aspect;
  const v3 c = eye + f * distance;
  v3 corners[4];
  const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
  for (int i = 0; i < 4; i++)
    corners[i] = c + r * (w * sx[i]) + u * (h * sy[i]);
  const ImU32 col = IM_COL32(80, 150, 255, 255);
  for (int i = 0; i < 4; i++) {
    pr.line(dl, eye, corners[i], col);
    pr.line(dl, corners[i], corners[(i + 1) % 4], col);
  }
}

// raylib's DrawGrid(slices, spacing) on the XZ plane: the first line of each direction
// darker, as raylib drew it.
void draw_grid(ImDrawList *dl, const projector &pr, int slices, float spacing) {
  const float half = (float)(slices / 2) * spacing;
  for (int i = -slices / 2; i <= slices / 2; i++) {
    const ImU32 col = i == -slices / 2 ? IM_COL32(128, 128, 128, 255) : IM_COL32(191, 191, 191, 255);
    const float t = (float)i * spacing;
    pr.line(dl, {t, 0, -half}, {t, 0, half}, col);
    pr.line(dl, {-half, 0, t}, {half, 0, t}, col);
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
  const camera3 orbit = orbit_camera(a);
  if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    // Pan in the view plane, as far as the mouse moved at the target's depth.
    a.follow_game_camera_3d = false;
    const v3 f = normalize(orbit.target - orbit.position);
    const v3 r = normalize(cross(f, orbit.up));
    const v3 u = cross(r, f);
    const float per_px = 2.0f * a.orbit_dist * std::tan(orbit.fovy * 0.5f * deg2rad) / size.y;
    const v3 move = r * (-io.MouseDelta.x * per_px) + u * (io.MouseDelta.y * per_px);
    a.orbit_target[0] += move.x;
    a.orbit_target[1] += move.y;
    a.orbit_target[2] += move.z;
  }
  if (hovered && io.MouseWheel != 0.0f)
    a.orbit_dist = std::clamp(a.orbit_dist * std::pow(0.87f, io.MouseWheel), 0.2f, 1e5f);

  // "Follow game camera": look through the game's own eye.
  camera3 cam = orbit;
  const bool through_game = a.follow_game_camera_3d && s.on;
  if (through_game) {
    cam.position = vec(s.cam);
    cam.target = vec(s.cam + 3);
    cam.up = vec(s.cam + 6);
    cam.fovy = s.cam[9];
  }
  const projector pr(cam, p0, size);

  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 p1{p0.x + size.x, p0.y + size.y};
  dl->PushClipRect(p0, p1, true);
  dl->AddRectFilled(p0, p1, background);
  // Grid scaled to the orbit distance: always a readable number of lines.
  float spacing = 1.0f;
  while (spacing * 40.0f < a.orbit_dist)
    spacing *= 10.0f;
  draw_grid(dl, pr, 40, spacing);
  if (!through_game) {
    draw_frustum(dl, pr, s, std::max(a.orbit_dist * 0.15f, 0.5f));
    // Sun: a line falling onto the orbit target.
    const v3 t = vec(a.orbit_target);
    pr.line(dl, t - normalize(vec(s.sun)) * (a.orbit_dist * 0.25f), t, IM_COL32(253, 249, 0, 255));
  }
  for (const gizmo_line_row &l : s.gizmo_lines)
    pr.line(dl, vec(l.a), vec(l.b), rgba_of(l.color));

  // Every draw and light as a point of a fixed size on screen, in its colour.
  ImVec2 sp{};
  for (const item3d_row &it : s.items) {
    if (!pr.point(it.pos, sp))
      continue;
    const ImU32 c = opaque(rgba_of(it.color));
    dl->AddCircleFilled(sp, 3.5f, darker(c));
    dl->AddCircleFilled(sp, 2.5f, c);
  }
  for (const light3d_row &l : s.lights) {
    if (!pr.point(l.pos, sp))
      continue;
    const float c4[4] = {l.color[0], l.color[1], l.color[2], 1.0f};
    dl->AddCircle(sp, 6.0f, rgba_of(c4));
    dl->AddCircleFilled(sp, 2.0f, rgba_of(c4));
  }
  if (!through_game && pr.point(s.cam, sp))
    dl->AddCircleFilled(sp, 4.0f, IM_COL32(80, 150, 255, 255));

  for (const gizmo_mark_row &m : s.gizmo_marks) {
    if (pr.point(m.pos, sp))
      draw_mark(dl, sp, m);
  }
  // The point under the mouse.
  if (hovered) {
    const item3d_row *hover = nullptr;
    ImVec2 hover_at{};
    float best = 8.0f * 8.0f;
    for (const item3d_row &it : s.items) {
      if (!pr.point(it.pos, sp))
        continue;
      const float dx = sp.x - io.MousePos.x, dy = sp.y - io.MousePos.y;
      if (dx * dx + dy * dy < best) {
        best = dx * dx + dy * dy;
        hover = &it;
        hover_at = sp;
      }
    }
    if (hover != nullptr) {
      dl->AddCircle(hover_at, 7.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
      ImGui::SetTooltip("%s\n%.2f, %.2f, %.2f", kind_name(hover->kind), hover->pos[0], hover->pos[1], hover->pos[2]);
    }
  }
  char buf[128];
  std::snprintf(buf, sizeof buf, "%zu points (%lld instanced), %zu lights   distance %.1f", s.items.size(),
                s.instanced, s.lights.size(), a.orbit_dist);
  dl->AddText({p0.x + 6, p0.y + size.y - 20}, IM_COL32(160, 165, 180, 255), buf);
  dl->PopClipRect();
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
  if (size.x < 50 || size.y < 50) {
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
