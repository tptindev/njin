#pragma once

// What of the city to draw this frame, and how finely (render_lod.cpp).
//
// The table is cut into square chunks; every building and prop is put in the
// chunk its middle is in, and its instances are laid out chunk after chunk,
// so a chunk is one range of a batch. Each frame view_cull_update() works out:
// - which chunks the camera sees (frustum culling: the screen's corners cast
//   onto the table at street and at roof height),
// - which of them are near enough for their fine detail (level of detail:
//   windows, balconies, stools and bikes only close up; nearer still when
//   something is in focus),
// draw_chunks() then draws only the ranges wanted, leaving out buildings open
// in the cutaway.

#include "render_common.h"

#include <utility>
#include <vector>

namespace sandtable::city {

inline constexpr f32 chunk_size = 100.0f; // world units

struct view_cull {
  i32 cols = 0, rows = 0;
  f32 x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the part of the table in view
  vec2 center{};       // where detail is kept: the focus, or the middle of the view
  f32 detail_r = 0.0f; // fine detail within this of `center`
  f32 prop_r = 0.0f;   // small props within this
  bool focused = false;
};

const view_cull &cull();
// The chunk grid for `map`: before anything is laid out chunk by chunk.
void chunk_grid(const city_map &map);
void view_cull_update(context &ctx, const city_map &map, const view_options &opt);

i32 chunk_count();
i32 chunk_of(vec2 p);
bool chunk_visible(i32 c);
bool chunk_detailed(i32 c, f32 radius);

// Instance ranges to leave out (buildings open in the cutaway), sorted.
using skip_list = std::vector<std::pair<u32, u32>>;

// A batch laid out chunk after chunk: `start[c]` its first instance of chunk
// `c`, `start[chunk_count()]` the end.
struct chunked {
  instances inst;
  std::vector<u32> start;
  void begin(i32 chunks) {
    inst.clear();
    start.assign(static_cast<size_t>(chunks) + 1, 0);
  }
  void mark(i32 chunk) { start[static_cast<size_t>(chunk)] = inst.count(); }
  void end() { start.back() = inst.count(); }
};

// Draws the chunks `want` says yes to (less `skip`), counting what went out.
template <typename Want>
void draw_chunks(context &ctx, const chunked &b, mesh3d_kind mesh, Want want, const skip_list *skip = nullptr);

// Implementation of the template: the ranges to draw, merged where they touch.
std::vector<std::pair<u32, u32>> chunk_ranges(const chunked &b, const std::vector<u8> &want, const skip_list *skip);
void draw_ranges(context &ctx, const chunked &b, mesh3d_kind mesh, const std::vector<std::pair<u32, u32>> &ranges);
void draw_ranges_model(context &ctx, const chunked &b, model_handle model,
                       const std::vector<std::pair<u32, u32>> &ranges);

template <typename Want>
void draw_chunks(context &ctx, const chunked &b, mesh3d_kind mesh, Want want, const skip_list *skip) {
  if (b.start.empty())
    return;
  std::vector<u8> w(static_cast<size_t>(chunk_count()));
  for (i32 c = 0; c < chunk_count(); ++c)
    w[static_cast<size_t>(c)] = chunk_visible(c) && want(c) ? 1 : 0;
  draw_ranges(ctx, b, mesh, chunk_ranges(b, w, skip));
}

// As draw_chunks(), for a batch of copies of a model.
template <typename Want>
void draw_chunks_model(context &ctx, const chunked &b, model_handle model, Want want, const skip_list *skip = nullptr) {
  if (b.start.empty() || model.id == 0 || b.inst.buffer.id == 0)
    return;
  std::vector<u8> w(static_cast<size_t>(chunk_count()));
  for (i32 c = 0; c < chunk_count(); ++c)
    w[static_cast<size_t>(c)] = chunk_visible(c) && want(c) ? 1 : 0;
  draw_ranges_model(ctx, b, model, chunk_ranges(b, w, skip));
}

// Counting for view_last_stats().
void count_detailed(i32 chunks);

} // namespace sandtable::city
