#pragma once
#include "njin_internal_only.h"

#include "_mod.h"
#include "njin_world3d.h"
#include <memory>
#include <vector>

namespace njin {
struct context;

// The outdoor world (njin_world3d.h). The CPU side (heights, splat weights,
// grass and scatter placement, wave and sky formulas) is njin_world3d.cpp; the
// GPU side (shaders, textures, meshes, the draws end_3d makes) is
// modules/world3d_draw.cpp, which keeps its objects in the slots below.

struct terrain3d_slot {
  bool alive = false;
  terrain3d_desc desc{}; // its pointers cleared
  i32 res = 0;           // samples per side
  f32 spacing = 1.0f;    // metres between two samples
  i32 chunk = 32;        // quads per chunk side
  i32 chunks = 0;        // chunks per side
  i32 lods = 1;
  f32 min_h = 0.0f, max_h = 0.0f; // world heights
  std::vector<f32> heights;       // res * res world heights, row z then column x
  std::vector<u8> base;           // res * res * 4 layer weights from the rules or the splat map
  std::vector<u8> paint;          // res * res * 4 weights painted by terrain3d_paint
  std::vector<u8> splat;          // res * res * 4 the two mixed: what is drawn and queried
  std::vector<vec2> chunk_y;      // lowest and highest point of each chunk (world3d_draw.cpp)
  body3d_handle body{};
  // Changed since the GPU copy was last updated: [x0, x1) x [z0, z1) samples.
  bool dirty = true;
  i32 dx0 = 0, dz0 = 0, dx1 = 0, dz1 = 0;
  // GPU (world3d_draw.cpp): heights (R32F), normals and layer weights (RGBA8).
  u32 height_tex = 0, normal_tex = 0, splat_tex = 0;
};

// One square of grass blades, made when the camera comes near and dropped when
// it has been out of range a while.
struct grass_cell {
  i32 cx = 0, cz = 0;
  u32 vbo = 0;    // 8 floats per blade
  u32 count = 0;
  bool stale = false; // the ground under it changed: grow it again
  u32 used = 0;       // the frame it was last in range
  vec3 lo{}, hi{};
};

struct grass3d_slot {
  bool alive = false;
  grass3d_desc desc{};
  f32 cell = 16.0f; // metres per cell side
  std::vector<grass_cell> cells;
  i32 drawn = 0;    // blades in the last pass
};

// What scatter3d places, kept so an edit of the ground can put it back on it.
struct scatter_item {
  f32 x = 0.0f, z = 0.0f, yaw = 0.0f, scale = 1.0f, shade = 1.0f;
};

struct scatter_cell {
  std::vector<scatter_item> items;
  instance_buffer_handle buffer{}; // 16 floats per instance (njin_3d.h)
  vec3 lo{}, hi{};
};

struct scatter3d_slot {
  bool alive = false;
  scatter3d_desc desc{};
  f32 cell = 64.0f;
  i32 per_side = 0;
  std::vector<scatter_cell> cells;
  i32 total = 0;
};

struct water3d_slot {
  bool alive = false;
  water3d_desc desc{};
};

// The sky as the shaders draw it (and water reflects it), from sky3d or, when
// no sky is drawn in the pass, from the light.
struct sky_params {
  vec3 sun{0.0f, 1.0f, 0.0f}; // towards the sun
  vec3 sun_color{1.0f, 1.0f, 1.0f};
  vec3 zenith{0.2f, 0.42f, 0.85f};
  vec3 horizon{0.62f, 0.78f, 0.95f};
  vec3 ground{0.3f, 0.3f, 0.3f};
  vec3 glow{1.0f, 0.45f, 0.15f}; // sunset colour round the sun
  f32 twilight = 0.0f;   // how much of that glow
  f32 day = 1.0f;        // 0 night, 1 day
  f32 clouds = 0.0f, cloud_darkness = 0.0f;
  f32 cloud_height = 900.0f, cloud_scale = 1.0f;
  f32 sun_size = 1.0f;
  f32 stars = 0.0f;      // how much of the starry sky shows
  vec2 wind{2.0f, 0.6f};
  f32 fog = 0.0f;
  vec3 fog_color{0.6f, 0.65f, 0.75f};
  f32 rain = 0.0f, snow = 0.0f;
};

struct world3d_gpu; // modules/world3d_draw.cpp

struct world3d_store {
  std::vector<terrain3d_slot> terrains; // handle id N is terrains[N - 1]
  std::vector<grass3d_slot> grasses;
  std::vector<scatter3d_slot> scatters;
  std::vector<water3d_slot> waters;
  f32 time = 0.0f;           // the waves' clock (water3d_time)
  vec2 wind{2.0f, 0.6f};     // wind3d_set
  f32 wetness = 0.0f;        // weather3d::wetness of the last sky drawn
  u32 frame = 0;
  // draw_sky3d in the open pass: its sky for the water's reflection. Reset at end_3d.
  bool sky_drawn = false;
  sky_params sky{};
  std::unique_ptr<world3d_gpu> gpu;
  world3d_store();
  ~world3d_store();
  world3d_store(const world3d_store &) = delete;
  world3d_store &operator=(const world3d_store &) = delete;
};

const terrain3d_slot *terrain_of(const context &ctx, terrain3d_handle handle);
terrain3d_slot *terrain_of(context &ctx, terrain3d_handle handle);

// Height and normal of a terrain at world (x, z), exactly as drawn at full
// detail and as the height field collides.
f32 terrain_height_at(const terrain3d_slot &t, f32 x, f32 z);
vec3 terrain_normal_at(const terrain3d_slot &t, f32 x, f32 z);
// The normal at sample (i, j), from its neighbours.
vec3 terrain_sample_normal(const terrain3d_slot &t, i32 i, i32 j);

// The blades of one grass cell: 8 floats each (position, height; facing,
// width, shade, phase). Empty when nothing grows there.
void grass_cell_blades(const terrain3d_slot &t, const grass3d_slot &g, i32 cx, i32 cz, std::vector<f32> &out);

// Gerstner displacement and normal at rest position (x, z) and time t, the
// same sums the water shader makes.
void water_displace(const water3d_desc &w, f32 x, f32 z, f32 t, vec3 &offset, vec3 &normal);

sky_params sky_params_of(const sky3d &sky);
sky_params sky_params_from_light(const context &ctx);

// Buoyancy: the surface under `at` for physics3d_step (njin_physics3d_impl.h).
bool world3d_water_surface(const context &ctx, u32 water, vec3 at, vec3 &point, vec3 &normal);

// GPU side (modules/world3d_draw.cpp).
void world3d_free_terrain(terrain3d_slot &t);
void world3d_free_grass(grass3d_slot &g);

// Core module: advances the waves' clock.
mod_desc world3d_module();
} // namespace njin
