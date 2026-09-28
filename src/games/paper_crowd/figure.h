#pragma once
// What draw.cpp and bake.cpp agree on about a figure: the pose ids person.fs
// switches on, the size of the quad it is drawn in, the instance a person is
// written as, and the layout of the baked sprite sheet.
#include "game.h"

namespace paper_crowd {
// Pose ids, in step with assets/person.fs.
enum pose_id : u32 {
  pose_stand,
  pose_walk,
  pose_run,
  pose_wave,
  pose_jump,
  pose_jacks,
  pose_dance,
  pose_cartwheel,
  pose_handstand,
  pose_lie,
  pose_sit,
  pose_ring,
};

// The quad, in figure units (one world pixel at size 1), and the point of the
// figure at its centre, which is also what tipping poses turn around. Both
// must match assets/person.fs.
inline constexpr f32 quad_units = 24.0f;
// Above this camera zoom the sheet's frames (4 px a unit, drawn at twice that
// on screen at zoom 1 with render_scale 2) would be magnified and soften, and
// few enough people are in view that computing them live is cheap.
inline constexpr f32 bake_zoom_max = 2.0f;
inline constexpr vec2 pivot{0.0f, -10.0f};

// One person's instance for draw_instanced(): three vec4s, read by person.vs
// as instance0..2. `mode` says how person.fs draws it:
//   live   (mode 1): a = pose id,   phase, extra, extra2 = the pose's parameters
//   baked  (mode 0): a = frame 0,   phase = blend toward frame 1, extra = frame 1
struct instance {
  f32 cx, cy, side, rotation; // quad centre, side length, degrees clockwise
  f32 a, cloth, flip, mode;   // pose or frame, colour index, -1 to face left, 1 live
  f32 phase, extra, extra2, seed;
};
static_assert(sizeof(instance) == 12 * sizeof(f32), "uploaded as is");

// ---- The baked sprite sheet
//
// Every pose is drawn once, at startup, by the same shader that draws a live
// person, into frames of cell_px squares laid out sheet_cols across. A frame
// stores not a colour but three numbers, so one sheet serves every colour of
// paper: how much of the person's own colour shows (R, /1.25), how much fixed
// ink and pale paper (G), and coverage (B). person.fs turns them into
// cloth * R + G at draw time.
inline constexpr i32 sheet_cols = 16;
inline constexpr i32 cell_px = 96; // 4 px per figure unit

// How to draw a pose from the sheet: the two frames to blend and by how much.
// False for a pose that is not baked (a ring's hands depend on the neighbours),
// which is drawn live.
struct sheet_pick {
  u32 f0 = 0;
  u32 f1 = 0;
  f32 blend = 0.0f;
};
bool sheet_frames(u32 pose, f32 phase, f32 extra, sheet_pick &out);

// Writes the sheet to the game's save folder (save_path) as three files: the
// sheet itself as a PNG, a PNG of the same frames in colour to look at, and a
// JSON of where each pose's frames are. `folder` is where they went. False if
// there is no sheet or a file could not be written.
bool export_sheet(njin_ctx &ctx, std::string &folder);

// Draws the whole sheet into g.sheet. Call once, in phase_post_update or
// phase_post_render.
void bake_sheet(njin_ctx &ctx);
} // namespace paper_crowd
