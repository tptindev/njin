// The sprite sheet: every pose drawn once by person.fs into a render texture,
// then read back by the same shader at draw time instead of being computed
// again for every person on every frame.
//
// A pose is a table row: how many frames its phase cycle gets, and how many
// headings it is baked for (walking and running lean their limbs along the
// direction of travel; everything else looks the same however it moves).
// Frames blend, so a cycle of 8 frames still moves smoothly, as long as the
// pose changes little between two frames (see the jumping jacks).
#include "figure.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace paper_crowd {
namespace {
struct row {
  pose_id pose;
  const char *name;
  u32 phases;
  u32 headings;
  bool smooth; // blend two frames; false picks the nearest, for a pose that cannot be blended
};

constexpr u32 heading_count = 5; // -90, -45, 0, 45, 90 degrees from "to the right"

constexpr row rows[] = {
    {pose_stand, "stand", 1, 1, true},
    {pose_walk, "walk", 8, heading_count, true},
    {pose_run, "run", 8, heading_count, true},
    {pose_wave, "wave", 8, 1, true},
    {pose_jump, "jump", 12, 1, true},
    // The arms sweep from beside the shoulder to overhead, and the elbow IK
    // finds bends out sideways as the hand passes the shoulder: frames a hand's
    // width apart differ far more than that, so a blend of two shows ghost arms.
    // 32 frames a half-second cycle is about one a displayed frame, so the
    // nearest one is enough.
    {pose_jacks, "jacks", 32, 1, false},
    {pose_dance, "dance", 12, 1, true},
    {pose_cartwheel, "cartwheel", 1, 1, true},
    {pose_handstand, "handstand", 6, 1, true},
    {pose_lie, "lie", 6, 1, true},
    {pose_sit, "sit", 1, 1, true},
};
constexpr u32 row_count = sizeof rows / sizeof rows[0];

constexpr u32 frames_of(u32 i) { return rows[i].phases * rows[i].headings; }

constexpr u32 base_of(u32 i) {
  u32 base = 0;
  for (u32 k = 0; k < i; ++k)
    base += frames_of(k);
  return base;
}

constexpr u32 total_frames() { return base_of(row_count - 1) + frames_of(row_count - 1); }

constexpr f32 pi = 3.14159265f;

// Heading buckets are a signed angle from "to the right": a person drawn
// facing left is mirrored, so the shader always sees them heading right (or
// straight up or down), which is why 180 degrees is never needed.
f32 heading_angle(u32 index) { return -90.0f + 45.0f * static_cast<f32>(index); }

// The `extra` a walking pose is baked with for a heading, the same 0..1 turn
// draw.cpp writes.
f32 heading_extra(u32 index) {
  const f32 turns = heading_angle(index) / 360.0f;
  return turns - std::floor(turns);
}
} // namespace

bool sheet_frames(u32 pose, f32 phase, f32 extra, sheet_pick &out) {
  u32 i = 0;
  while (i < row_count && rows[i].pose != pose)
    ++i;
  if (i == row_count)
    return false;
  const row &r = rows[i];

  u32 heading = 0;
  if (r.headings > 1) {
    const f32 degrees = (extra > 0.5f ? extra - 1.0f : extra) * 360.0f;
    heading = static_cast<u32>(std::clamp(std::lround((std::clamp(degrees, -90.0f, 90.0f) + 90.0f) / 45.0f), 0L,
                                          static_cast<long>(r.headings) - 1));
  }
  const u32 first = base_of(i) + heading * r.phases;
  const f32 at = (phase - std::floor(phase)) * static_cast<f32>(r.phases);
  if (!r.smooth) {
    out.f0 = first + static_cast<u32>(std::lround(at)) % r.phases;
    out.f1 = out.f0;
    out.blend = 0.0f;
    return true;
  }
  const u32 i0 = std::min(static_cast<u32>(at), r.phases - 1);
  out.f0 = first + i0;
  out.f1 = first + (i0 + 1) % r.phases;
  out.blend = r.phases > 1 ? at - static_cast<f32>(i0) : 0.0f;
  return true;
}

void bake_sheet(njin_ctx &ctx) {
  constexpr u32 frames = total_frames();
  constexpr i32 sheet_rows = (static_cast<i32>(frames) + sheet_cols - 1) / sheet_cols;
  if (g.sheet.id != 0 || !g.person.id || !g.instances.id)
    return;
  g.sheet = render_texture_load(ctx, static_cast<u32>(sheet_cols * cell_px), static_cast<u32>(sheet_rows * cell_px));
  if (g.sheet.id == 0)
    return;
  // Frames are read at sizes other than the one they were drawn at.
  render_texture_set_filter(ctx, g.sheet, filter_linear);
  shader_set_vec2(ctx, g.person, "sheet_cells", {static_cast<f32>(sheet_cols), static_cast<f32>(sheet_rows)});

  // One instance a frame, each a live person of no colour in the cell it will
  // be read from. `mode` 1 with person.fs's `bake` on writes the three numbers
  // above instead of a colour.
  std::vector<instance> cells;
  cells.reserve(frames);
  for (u32 i = 0; i < row_count; ++i) {
    for (u32 h = 0; h < rows[i].headings; ++h) {
      for (u32 k = 0; k < rows[i].phases; ++k) {
        const u32 index = base_of(i) + h * rows[i].phases + k;
        const f32 cx = static_cast<f32>(static_cast<i32>(index) % sheet_cols * cell_px + cell_px / 2);
        const f32 cy = static_cast<f32>(static_cast<i32>(index) / sheet_cols * cell_px + cell_px / 2);
        cells.push_back({cx, cy, static_cast<f32>(cell_px), 0.0f, static_cast<f32>(rows[i].pose), 0.0f, 1.0f, 1.0f,
                         static_cast<f32>(k) / static_cast<f32>(rows[i].phases),
                         rows[i].headings > 1 ? heading_extra(h) : 0.0f, 0.0f, 0.3f});
      }
    }
  }
  instance_buffer_upload(ctx, g.instances, &cells.data()->cx, static_cast<u32>(cells.size()));

  render_texture_begin(ctx, g.sheet, rgba{0.0f, 0.0f, 0.0f, 1.0f});
  shader_set_i32(ctx, g.person, "bake", 1);
  draw_instanced(ctx, g.instances, g.person, 0, static_cast<u32>(cells.size()));
  shader_set_i32(ctx, g.person, "bake", 0);
  render_texture_end(ctx);
}

bool export_sheet(njin_ctx &ctx, std::string &folder) {
  constexpr u32 frames = total_frames();
  constexpr i32 sheet_rows = (static_cast<i32>(frames) + sheet_cols - 1) / sheet_cols;
  if (g.sheet.id == 0)
    return false;
  const std::string data_png = save_path(ctx, "sheet/paper_crowd_sheet.png");
  const std::string preview_png = save_path(ctx, "sheet/paper_crowd_sheet_preview.png");
  const std::string layout_json = save_path(ctx, "sheet/paper_crowd_sheet.json");

  // The sheet as it is: three numbers a texel (see figure.h), exactly the bytes
  // person.fs reads, so it can be loaded back instead of baked.
  bool ok = render_texture_save(ctx, g.sheet, data_png.c_str());

  // A sheet to look at: the same frames drawn through the game's own baked
  // path, each in a colour of its own, over the paper.
  const render_texture_handle preview =
      render_texture_load(ctx, static_cast<u32>(sheet_cols * cell_px), static_cast<u32>(sheet_rows * cell_px));
  if (preview.id != 0) {
    std::vector<instance> cells;
    cells.reserve(frames);
    for (u32 index = 0; index < frames; ++index) {
      const f32 cx = static_cast<f32>(static_cast<i32>(index) % sheet_cols * cell_px + cell_px / 2);
      const f32 cy = static_cast<f32>(static_cast<i32>(index) / sheet_cols * cell_px + cell_px / 2);
      cells.push_back({cx, cy, static_cast<f32>(cell_px), 0.0f, static_cast<f32>(index),
                       static_cast<f32>(index % cloth_colors.size()), 1.0f, 0.0f, 0.0f, static_cast<f32>(index), 0.0f,
                       0.0f});
    }
    instance_buffer_upload(ctx, g.instances, &cells.data()->cx, static_cast<u32>(cells.size()));
    render_texture_begin(ctx, preview, rgba{0.945f, 0.94f, 0.925f, 1.0f});
    draw_instanced(ctx, g.instances, g.person, 0, static_cast<u32>(cells.size()), g.sheet);
    render_texture_end(ctx);
    ok = render_texture_save(ctx, preview, preview_png.c_str()) && ok;
    render_texture_unload(ctx, preview);
  } else {
    ok = false;
  }

  // Where each pose's frames are, so the PNG is usable without this source.
  json_value poses = json_value::make_array();
  for (u32 i = 0; i < row_count; ++i) {
    json_value row_json = json_value::make_object();
    row_json.set("name", rows[i].name)
        .set("pose_id", static_cast<i32>(rows[i].pose))
        .set("first_frame", static_cast<i32>(base_of(i)))
        .set("phases", static_cast<i32>(rows[i].phases))
        .set("headings", static_cast<i32>(rows[i].headings))
        .set("blend_neighbours", rows[i].smooth);
    poses.push(row_json);
  }
  json_value headings = json_value::make_array();
  for (u32 h = 0; h < heading_count; ++h)
    headings.push(json_value(static_cast<f64>(heading_angle(h))));
  json_value channels = json_value::make_object();
  channels.set("R", "share of the person's own paper colour, divided by 1.25")
      .set("G", "fixed ink and pale paper")
      .set("B", "coverage")
      .set("A", "opaque");
  json_value layout = json_value::make_object();
  layout.set("image", "paper_crowd_sheet.png")
      .set("frames", static_cast<i32>(frames))
      .set("columns", sheet_cols)
      .set("rows", sheet_rows)
      .set("cell_px", cell_px)
      .set("px_per_figure_unit", cell_px / quad_units)
      .set("figure_origin_px", json_value(json_value::make_array().push(cell_px / 2.0).push(cell_px / 2.0 - pivot.y * (cell_px / quad_units))))
      .set("colour", "paper * (R * 1.25) + G, divided by B; alpha is B. Premultiplied: filter before dividing.")
      .set("frame_index", "row-major: column = index % columns, row = index / columns, top-left origin")
      .set("walk_run_headings_degrees", headings)
      .set("channels", channels)
      .set("poses", poses);
  ok = json_save(layout_json.c_str(), layout) && ok;

  folder = layout_json.substr(0, layout_json.find_last_of("/\\"));
  return ok;
}
} // namespace paper_crowd
