#pragma once
#include "../njin_internal_only.h"
#include "lighting.h"
#include <vector>

namespace njin {
// Shared by the files of the lighting module: lighting.cpp (the passes), lighting_shaders.cpp (the
// GLSL), lighting_shapes.cpp (outlines of cells and tiles), lighting_occluders.cpp (the edges that
// cast shadows).
namespace light_impl {

// --- lighting_shaders.cpp ---

extern const char *const light_vs;     // one light: quad in the world, the pixel's world position
extern const char *const light_fs;     // one light: Cook-Torrance, shadows from the edge buckets
extern const char *const ambient_fs;   // the start of the HDR image: ambient on the albedo
extern const char *const final_fs;     // exposure, tonemap, gamma
extern const char *const march_fs;     // the 1D shadow map of a light, marched through the occluder map

// --- lighting_targets.cpp ---

// (Re)creates `target` at `w`x`h` when its size differs.
bool ensure_target(RenderTexture2D &target, i32 w, i32 h, bool bilinear);
// Like ensure_target(), but the colour buffer is 16-bit float (32-bit with `full_precision`, for distances). Falls back to an ordinary image (and says so
// once, clearing `float_ok`) where that is not possible.
bool ensure_float_target(RenderTexture2D &target, i32 w, i32 h, bool &float_ok, bool full_precision = false);

// --- lighting_shapes.cpp ---

using cell_list = std::vector<cell>;

f32 signed_area(const std::vector<vec2> &points);
// The outlines of a set of cells, as closed loops in cell units: one around each connected block and one
// around each hole in it, the latter marked. Straight runs are merged into one edge; `simplify` (cells)
// then smooths what is left.
std::vector<silhouette_loop> outline_cells(const cell_list &cells, f32 simplify);

// What one light needs, gathered before the light image is drawn.
struct light_job {
  const light_2d *light;
  vec2 pos;
  f32 angle; // radians
  rect area; // where it can light: the box its quad covers
};

// The strips a directional light's shadows are sorted into, across its rays: the axis across them (xy), where
// the first one starts along it (z) and how many there are per world unit times 32 (w), over the box `view`.
vec4 directional_bins(const rect &view, f32 angle);

// --- lighting_pixel_shadows.cpp ---

// What the light shader needs to read one light's row of the pixel shadow map.
struct pixel_light {
  i32 row = -1; // -1: this light has no pixel shadows
  f32 inv = 0.0f, off = 0.0f, tol = 0.0f;
};

// Pixel-perfect shadows (mattdesl, "2D Pixel-Perfect Shadows"): draws the sprites with light_occluder_pixels into
// the occluder map, then one row of the shadow map per light by ray-marching it. `out[i]` describes light i.
// Returns whether any row was made (so the shader has a texture to read).
bool build_pixel_shadows(context &ctx, lighting_state &s, const Camera2D &camera, const rect &steady, f32 texel_scale,
                         const std::vector<light_job> &lights, std::vector<pixel_light> &out);

// --- lighting_occluders.cpp ---

// The edge of an occluder, in the world, with the box around it. Edges of one occluder sit together
// (`owner_first`, `owner_count`), so a light can tell whether it is inside it.
struct occluder_edge {
  vec2 a, b;
  f32 min_x, min_y, max_x, max_y;
  u32 owner_first = 0, owner_count = 0;
  bool solid = false; // an edge of a closed shape (one sided), not of a wall
};

inline bool overlaps(const rect &a, f32 min_x, f32 min_y, f32 max_x, f32 max_y) {
  return a.pos.x <= max_x && a.pos.x + a.size.x >= min_x && a.pos.y <= max_y && a.pos.y + a.size.y >= min_y;
}

// True when `p` is inside the solid a set of edges bounds (even-odd, so holes count).
bool inside_owner(const std::vector<occluder_edge> &edges, const occluder_edge &any_edge, vec2 p);

// Every occluder that touches one of `areas` (where the lights reach) as edges: the shapes of
// light_occluder, and the outlines of light_occluder_sprite frames.
void gather_edges(context &ctx, lighting_state &s, std::vector<occluder_edge> &edges, const std::vector<rect> &areas);
} // namespace light_impl
} // namespace njin
