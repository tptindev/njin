#pragma once
#include "../njin_internal_only.h"
#include "njin_3d.h"
#include "njin_gizmo.h"
#include <string>
#include <vector>

namespace njin {
struct context;

// Debug gizmos (njin_gizmo.h). Every call is broken down at once into what is
// drawn: line segments, points (a cross of fixed screen size) and labels, in
// 2D or 3D. They live `left` more seconds of real time; gizmo_frame_begin
// ages them and drops the finished ones, so a 0-second gizmo lasts exactly
// the frame it was added in, whichever phase added it.
//
// Drawing, all on top of the world:
//   3D lines    at end_3d, through that pass's camera, depth test off
//               (render3d.cpp); the 3D points and labels are projected to
//               the screen there
//   the rest    2D lines, points and labels, in screen pixels at the start of
//               the UI pass (camera.cpp), through the 2D camera (w2scr)
// The debug module sends them to the inspector's World view too.

struct gizmo_line2d {
  vec2 a, b;
  rgba color;
  f32 left;
};
struct gizmo_mark2d { // a point (text empty) or a label
  vec2 pos;
  std::string text;
  rgba color;
  f32 left;
};
struct gizmo_line3d_item {
  vec3 a, b;
  rgba color;
  f32 left;
};
struct gizmo_mark3d {
  vec3 pos;
  std::string text;
  rgba color;
  f32 left;
};

struct gizmo_state {
  bool visible = true;
  std::vector<gizmo_line2d> lines;
  std::vector<gizmo_mark2d> marks;
  std::vector<gizmo_line3d_item> lines3d;
  std::vector<gizmo_mark3d> marks3d;
  // 3D points and labels as the last 3D pass saw them on screen, drawn with
  // the 2D ones in the UI pass.
  std::vector<gizmo_mark2d> projected;
};

// Ages every gizmo by the frame's real time and drops the finished ones.
// Called once per frame, before any system runs.
void gizmo_frame_begin(context &ctx);
// 3D lines, inside a 3D pass seen through `camera`; projects the 3D marks.
void gizmo_draw_3d(context &ctx, const camera3d &camera);
// 2D lines, points and labels, in screen space.
void gizmo_draw_screen(context &ctx);
} // namespace njin
