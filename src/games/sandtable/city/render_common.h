#pragma once

// Inside the city renderer: the helpers the render_*.cpp files share.

#include "../view.h"
#include "render.h"

#include <vector>

namespace sandtable::city {

// --- Heights of the flat layers, 3D units ---------------------------------------
//
// Far apart enough that the depth buffer keeps them apart from the whole-table
// view.

inline constexpr f32 layer_water = -0.02f;   // raster water, under everything
inline constexpr f32 layer_ground = 0.0f;
inline constexpr f32 layer_rim = 0.004f;     // stone rim of river and lake
inline constexpr f32 layer_surface = 0.008f; // water surfaces, open places
inline constexpr f32 layer_sidewalk = 0.02f;
inline constexpr f32 layer_alley = 0.025f;
inline constexpr f32 layer_road = 0.03f;
inline constexpr f32 layer_marking = 0.04f;
inline constexpr f32 layer_island = 0.05f;

// --- Colour ----------------------------------------------------------------------

constexpr rgba rgb8(i32 r, i32 g, i32 b) { return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f}; }
inline rgba shade(rgba c, f32 k) { return {c.r * k, c.g * k, c.b * k, c.a}; }

// A number from `look` and a salt, spread evenly: 0 to 1, or an index.
u32 mix(u32 look, u32 salt);
inline f32 pick01(u32 look, u32 salt) { return static_cast<f32>(mix(look, salt) % 10000u) / 10000.0f; }
template <typename T, size_t N> const T &pick(const T (&arr)[N], u32 look, u32 salt) {
  return arr[mix(look, salt) % N];
}

rgba district_color(district_kind k);        // the overlay's colour for a district
rgba business_color(business_kind k);        // signs and pins
rgba ground_color(const city_map &map, const cell_info &c);

// --- Instances of one engine mesh (draw_instanced3d) ------------------------------

struct instances {
  instance_buffer_handle buffer{};
  std::vector<f32> data;

  u32 count() const { return static_cast<u32>(data.size() / 16); }
  void clear() { data.clear(); }
  // In 3D units: position (the mesh's origin), size, colour, turn round y,
  // and tip round the mesh's own x first (degrees).
  void add3(vec3 pos, vec3 size, rgba col, f32 yaw = 0.0f, f32 pitch = 0.0f);
  // A box in table terms: centred on `at`, standing on `base` (world units
  // up), `size` = (along `angle`, height, across), world units.
  void box(vec2 at, f32 base, vec3 size, f32 angle, rgba col);
  // A box tipped `pitch` degrees round its own width (a roof slope): centred
  // on `at` at `mid` world units up, `size` as for box().
  void tilted(vec2 at, f32 mid, vec3 size, f32 angle, f32 pitch, rgba col);
  // An upright cylinder (mesh3d_cylinder*: radius 1, height 1, base at 0).
  void post(vec2 at, f32 base, f32 radius, f32 height, rgba col);
  // A ball (mesh3d_sphere*: radius 1, centre at 0), `lift` world units up.
  void ball(vec2 at, f32 lift, f32 radius, rgba col);
  void upload(context &ctx);
  void draw(context &ctx, mesh3d_kind mesh) const;
  // Instances [from, to) only.
  void draw_range(context &ctx, mesh3d_kind mesh, u32 from, u32 to) const;
  void destroy(context &ctx);
};

// --- Flat coloured triangles, turned into models ---------------------------------

struct mesh_builder {
  std::vector<vec3> pos;
  std::vector<rgba> col;

  // Table coordinates at height y (3D units); faces up whatever the order.
  void tri(vec2 a, vec2 b, vec2 c, f32 y, rgba color);
  void quad(vec2 a, vec2 b, vec2 c, vec2 d, f32 y, rgba color);
  void rect(const obb &box, f32 y, rgba color);
  void disc(vec2 c, f32 r, f32 y, rgba color, i32 sides = 16);
  // A band `width` wide along a polyline; round joints between segments, and
  // square ends unless `closed`.
  void band(const std::vector<vec2> &pts, f32 width, f32 y, rgba color);
  // A thin line of `width` from a to b.
  void line(vec2 a, vec2 b, f32 width, f32 y, rgba color);
};

// Models made from a builder: a square tile of the table (world units) per
// model, so the engine culls the tiles out of view; a big tile in pieces the
// engine takes.
inline constexpr f32 mesh_tile = 400.0f;
struct mesh_set {
  std::vector<model_handle> models;
  void build(context &ctx, mesh_builder &b);
  void draw(context &ctx) const;
  void destroy(context &ctx);
};

// --- The parts, each in its own file ----------------------------------------------

void ground_build(context &ctx, const city_map &map);       // render_ground.cpp
void ground_draw(context &ctx, const city_map &map, const view_options &opt);
void ground_cleanup(context &ctx);


void props_build(context &ctx, const city_map &map);        // render_props.cpp
void props_draw(context &ctx, const view_options &opt);
void props_cleanup(context &ctx);

void cutaway_init(context &ctx); // render_cutaway.cpp: loads the interior kit's models once
void cutaway_draw(context &ctx, const city_map &map, const view_options &opt);
void cutaway_cleanup(context &ctx);    // every regen: the per-frame instance buffers
void cutaway_shutdown(context &ctx);   // game exit only: the kit's models

void debug_build(context &ctx, const city_map &map);        // render_debug.cpp
void debug_draw(context &ctx, const city_map &map, const view_options &opt);
void debug_cleanup(context &ctx);

void hover_build(context &ctx, const city_map &map);        // render_hover.cpp
void hover_draw(context &ctx, const city_map &map, const view_options &opt);
void hover_label(context &ctx, const city_map &map, const view_options &opt, font_handle font);
void hover_cleanup(context &ctx);

} // namespace sandtable::city
