#include "game.h"
#include <cstdlib>

// Two sheets, baked once:
//   body  one cell per (build, pose, base direction, frame), two render
//         textures (A: cloth, far limb, near limb; B: shoe + head centre)
//   head  one cell per (hair style, base direction)
// Keeping the head apart means hair styles cost 20 small cells each instead of
// a copy of every body pose.

namespace crowd {
namespace {
using namespace njin;

constexpr u8 d_s = 1, d_e = 4, d_n = 16, d_all = 31;
constexpr pose_def pose_defs[pose_count] = {
    {"idle", 4, d_all},      {"walk", 6, d_all},      {"run", 6, d_all},  {"jump", 4, d_all},
    {"sit", 2, d_all},       {"lie", 2, d_s},         {"wave", 4, d_all}, {"shake", 4, d_e},
    {"hold_l", 7, d_s | d_n}, {"hold_r", 7, d_s | d_n}, {"hold_b", 7, d_s | d_n},
    {"punch", 4, d_all},     {"kick", 4, d_all},
    {"leap_punch", 4, d_all}, {"leap_kick", 4, d_all},
};

constexpr u32 count_cells() {
  u32 n = 0;
  for (const pose_def &p : pose_defs)
    for (u32 b = 0; b < base_dirs; b++)
      if (p.dirs >> b & 1u)
        n += p.frames;
  return n;
}
constexpr u32 cells_per_build = count_cells();
constexpr u32 sheet_cols = 32;
constexpr u32 rows_per_build = (cells_per_build + sheet_cols - 1) / sheet_cols;
constexpr u32 sheet_w = sheet_cols * cell_w, sheet_h = builds * rows_per_build * cell_h;

constexpr u32 head_w = 24, head_h = 32;
constexpr vec2 head_center{12.0f, 12.0f}; // pixels from the head cell's top-left
constexpr u32 head_sheet_w = base_dirs * head_w, head_sheet_h = hair_styles * head_h;

constexpr f32 foot_px = (-0.080f - shape_bottom) * shape_scale;

struct sheet_state {
  shader_handle bake, draw;
  render_texture_handle body[2], head;
  instance_buffer_handle bake_body, bake_head, crowd;
  i32 cell_start[pose_count][base_dirs];
  bool baked = false;
};
sheet_state s;

vec2 cell_center(u32 col, u32 row, u32 w, u32 h) {
  return {((f32)col + 0.5f) * (f32)w, ((f32)row + 0.5f) * (f32)h};
}
} // namespace

const pose_def &pose_desc(pose_id pose) { return pose_defs[pose]; }

cell_ref cell_for(pose_id pose, u8 dir, u32 frame) {
  static constexpr u8 base_of[8] = {0, 1, 2, 3, 4, 3, 2, 1};
  const bool mirrored = dir >= dir_nw;
  i32 base = base_of[dir & 7];
  if (s.cell_start[pose][base] < 0) {
    i32 best = -1;
    for (i32 b = 0; b < (i32)base_dirs; b++)
      if (s.cell_start[pose][b] >= 0 && (best < 0 || std::abs(b - base) < std::abs(best - base)))
        best = b;
    base = best;
  }
  const u32 frames = pose_defs[pose].frames;
  const f32 cell = (f32)(s.cell_start[pose][base] + (i32)(frame % frames) + 1);
  // Only mirror a base direction that has a mirror (not S or N).
  const bool flip = mirrored && base != 0 && base != 4;
  return {flip ? -cell : cell, (u8)base};
}

bool sheet_load(context &ctx) {
  if (!instancing_available(ctx))
    return false;
  u32 next = 0;
  for (u32 p = 0; p < pose_count; p++)
    for (u32 b = 0; b < base_dirs; b++) {
      s.cell_start[p][b] = (pose_defs[p].dirs >> b & 1u) ? (i32)next : -1;
      if (s.cell_start[p][b] >= 0)
        next += pose_defs[p].frames;
    }

  s.bake = shader_load(ctx, "assets/shaders/bake.vs", "assets/shaders/bake.fs");
  s.draw = shader_load(ctx, "assets/shaders/crowd.vs", "assets/shaders/crowd.fs");
  for (render_texture_handle &t : s.body)
    t = render_texture_load(ctx, sheet_w, sheet_h);
  s.head = render_texture_load(ctx, head_sheet_w, head_sheet_h);
  render_texture_set_filter(ctx, s.body[0], filter_linear);
  render_texture_set_filter(ctx, s.body[1], filter_linear);
  render_texture_set_filter(ctx, s.head, filter_linear);
  s.bake_body = instance_buffer_create(ctx, 8);
  s.bake_head = instance_buffer_create(ctx, 8);
  s.crowd = instance_buffer_create(ctx, 8);

  shader_set_f32(ctx, s.bake, "u_scale", shape_scale);
  shader_set_f32(ctx, s.bake, "u_bottom", shape_bottom);
  shader_set_vec2(ctx, s.bake, "u_head_center", head_center);

  const shader_handle d = s.draw;
  shader_set_vec2(ctx, d, "u_cell", {(f32)cell_w, (f32)cell_h});
  shader_set_vec2(ctx, d, "u_sheet", {(f32)sheet_w, (f32)sheet_h});
  shader_set_f32(ctx, d, "u_cols", (f32)sheet_cols);
  shader_set_f32(ctx, d, "u_rows_per_build", (f32)rows_per_build);
  shader_set_vec2(ctx, d, "u_head_cell", {(f32)head_w, (f32)head_h});
  shader_set_vec2(ctx, d, "u_head_center", head_center);
  shader_set_vec2(ctx, d, "u_head_sheet", {(f32)head_sheet_w, (f32)head_sheet_h});
  shader_set_f32(ctx, d, "u_foot", foot_px);
  shader_set_f32(ctx, d, "u_world", world_per_px);
  shader_set_f32(ctx, d, "u_scale", shape_scale);
  shader_set_f32(ctx, d, "u_bottom", shape_bottom);
  shader_set_texture(ctx, d, "sheet_b", s.body[1]);
  shader_set_texture(ctx, d, "head_sheet", s.head);
  return true;
}

void sheet_bake(context &ctx) {
  if (s.baked || s.bake.id == 0)
    return;
  s.baked = true;

  // Body: instance0 = centre, frame, frame count; instance1 = direction, pose, build, -.
  std::vector<f32> body;
  for (u32 build = 0; build < builds; build++)
    for (u32 p = 0; p < pose_count; p++)
      for (u32 b = 0; b < base_dirs; b++) {
        if (s.cell_start[p][b] < 0)
          continue;
        for (u32 f = 0; f < pose_defs[p].frames; f++) {
          const u32 index = (u32)s.cell_start[p][b] + f;
          const vec2 c = cell_center(index % sheet_cols, build * rows_per_build + index / sheet_cols, cell_w, cell_h);
          body.insert(body.end(), {c.x, c.y, (f32)f, (f32)pose_defs[p].frames, (f32)b, (f32)p, (f32)build, 0.0f});
        }
      }
  const u32 body_count = (u32)body.size() / 8;
  instance_buffer_upload(ctx, s.bake_body, body.data(), body_count);
  shader_set_vec2(ctx, s.bake, "u_cell", {(f32)cell_w, (f32)cell_h});
  for (i32 pass = 0; pass < 2; pass++) {
    shader_set_i32(ctx, s.bake, "u_pass", pass);
    render_texture_begin(ctx, s.body[pass], rgba{0, 0, 0, 0});
    draw_instanced(ctx, s.bake_body, s.bake, 0, body_count);
    render_texture_end(ctx);
  }

  // Head: one cell per (hair style, direction).
  std::vector<f32> head;
  for (u32 style = 0; style < hair_styles; style++)
    for (u32 b = 0; b < base_dirs; b++) {
      const vec2 c = cell_center(b, style, head_w, head_h);
      head.insert(head.end(), {c.x, c.y, 0.0f, 1.0f, (f32)b, 0.0f, 0.0f, (f32)style});
    }
  const u32 head_count = (u32)head.size() / 8;
  instance_buffer_upload(ctx, s.bake_head, head.data(), head_count);
  shader_set_vec2(ctx, s.bake, "u_cell", {(f32)head_w, (f32)head_h});
  shader_set_i32(ctx, s.bake, "u_pass", 2);
  render_texture_begin(ctx, s.head, rgba{0, 0, 0, 0});
  draw_instanced(ctx, s.bake_head, s.bake, 0, head_count);
  render_texture_end(ctx);
}

bool sheet_ready() { return s.baked; }

sheet_stats sheet_get_stats() {
  return {builds * cells_per_build, sheet_w, sheet_h, head_sheet_w, head_sheet_h,
          (2 * sheet_w * sheet_h + head_sheet_w * head_sheet_h) * 4};
}

void sheet_draw(context &ctx, const std::vector<instance> &instances) {
  if (!s.baked || instances.empty())
    return;
  instance_buffer_upload(ctx, s.crowd, &instances.data()->x, (u32)instances.size());
  draw_instanced(ctx, s.crowd, s.draw, 0, (u32)instances.size(), s.body[0]);
}

bool sheet_save(context &ctx, std::string &folder) {
  if (!s.baked)
    return false;
  folder = save_path(ctx, "sheet_*.png");
  return render_texture_save(ctx, s.body[0], save_path(ctx, "sheet_body_a.png").c_str()) &&
         render_texture_save(ctx, s.body[1], save_path(ctx, "sheet_body_b.png").c_str()) &&
         render_texture_save(ctx, s.head, save_path(ctx, "sheet_head.png").c_str());
}
} // namespace crowd
