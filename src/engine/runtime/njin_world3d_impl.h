#pragma once
#include "njin_internal_only.h"

#include "_mod.h"
#include "njin_world3d.h"
#include "njin_renderer.h"
#include <deque>
#include <memory>
#include <vector>

namespace njin {
struct context;

// The outdoor world (njin_world3d.h). The CPU side (heights, splat weights,
// grass and scatter placement, wave and sky formulas) is njin_world3d.cpp; the
// GPU side (shaders, textures, meshes, the draws end_3d makes) is
// modules/world3d_draw.cpp, which keeps its objects in the slots below.

// Temporary marks on a terrain (terrain3d_imprint_*, njin_world3d_imprint.cpp):
// `desc.size` texels a side, kept round the camera and addressed by absolute
// texel coordinate modulo the size, so following the camera only clears the
// strip coming in. Four bytes a texel: pressed, rim, and the stamp's time in
// hundredths of a second (16 bits, little end first); the shader fades a mark
// by its age, so nothing is touched while marks fade.
struct terrain_imprint {
  struct shape {
    i32 w = 0, h = 0;
    std::vector<u8> pressed_rim;
  };
  struct mark {
    i32 x0, z0, x1, z1; // absolute texels, half-open
    f32 born;
  };
  terrain3d_imprint_desc desc{.size=0}; // size 0: off
  std::vector<u8> texels;
  std::vector<shape> shapes;
  std::deque<mark> marks;   // oldest first, for clearing when they have faded
  std::vector<mark> dirty;  // to upload (born unused)
  i32 x0 = 0, z0 = 0;       // the square's first texel, absolute
  bool placed = false;
  u32 tex = 0;              // GPU copy (world3d_draw.cpp), RGBA8, nearest
  i32 tex_size = 0;
  RenderTextureHandle portable_texture;
};

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
  // Counts the edits (mark_dirty), so the ray traced copy (render3d_rt.cpp)
  // knows when to rebuild.
  u32 edits = 0;
  // GPU (world3d_draw.cpp): heights (R32F), normals and layer weights (RGBA8).
  u32 height_tex = 0, normal_tex = 0, splat_tex = 0;
  // A layer with a roughness image: its normal map (flat if none) with the
  // roughness in alpha, one texture so the shader binds no more units. Made
  // from `pack_from` (normal id and version, roughness id and version), again
  // when one of those changes (hot reload).
  u32 layer_pack[4] = {0, 0, 0, 0};
  u32 pack_from[4][4] = {};
  // terrain3d_layer::relief: the layers' height images, layer l in channel l
  // (0.5 where a layer has none), made from `relief_from` (each layer's height
  // id and version), again when one of those changes.
  u32 relief_tex = 0;
  u32 relief_from[8] = {};
  terrain_imprint imprint;
  struct portable_chunk {std::vector<RenderMeshHandle> lods,relief;vec3 lo{},hi{};};
  std::vector<portable_chunk> portable_chunks;
  u32 portable_edits=~0u;
  std::array<RenderTextureHandle,4> portable_roughness{};
  std::array<u64,4> portable_roughness_keys{};
  std::array<RenderTextureHandle,3> portable_relief_textures{};
  std::array<u64,4> portable_relief_keys{};
  u32 portable_relief_edits=~0u;
  u64 portable_relief_frame=~0ull;
  vec3 portable_relief_eye{},portable_relief_previous_eye{};
};

inline constexpr usize grass_blade_floats = 12;

// One square of grass blades, made when the camera comes near and dropped when
// it has been out of range a while.
struct grass_cell {
  i32 cx = 0, cz = 0;
  u32 vbo = 0;    // grass_blade_floats per blade
  u32 count = 0;
  bool stale = false; // the ground under it changed: grow it again
  u32 used = 0;       // the frame it was last in range
  vec3 lo{}, hi{};
};

// A terrain layer's image, small and on the CPU, for the colour of the ground
// each blade grows from (grass3d_desc::ground_color): read once per blade
// when its cell is made, not by the shader at every vertex.
struct grass_ground_image {
  std::vector<u8> rgba;
  i32 size = 0;      // texels a side; 0: the layer has no image
  u32 tex = 0, version = 0; // the texture it was made from
};

struct grass3d_slot {
  bool alive = false;
  grass3d_desc desc{};
  f32 cell = 16.0f; // metres per cell side
  std::vector<grass_cell> cells;
  i32 drawn = 0;    // blades in the last pass
  std::vector<std::vector<float>> portable_cells;
  std::vector<bool> portable_ready;
  u32 portable_edits=~0u;
  grass_ground_image ground[4]; // by terrain layer (world3d_draw.cpp keeps them current)
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

// Whether a live water refracts or reflects the scene (water3d_desc::refract,
// water3d_desc::reflect), so the world pass
// must copy the scene's colour and depth for it (post3d_wanted).
bool world3d_wants_scene(const context &ctx);

// A ring of waves running out from where something fell in (water3d_splash).
struct water3d_ring {
  vec2 at{0.0f, 0.0f};
  f32 start = -1e9f; // on the waves' clock
  f32 strength = 0.0f;
  f32 size = 0.0f;
};

struct water3d_slot {
  bool alive = false;
  water3d_desc desc{};
  water3d_ring rings[water3d_splash_max]{};
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
  // How far the wind has blown since the start, summed each frame: clouds and
  // rain drift by it, so a wind that changes moves them on, never jumps them.
  vec2 drift{0.0f, 0.0f};
  f32 wetness = 0.0f;        // weather3d::wetness of the last sky drawn
  bool cover_auto = true;    // weather3d::cover_auto of the last sky drawn
  std::vector<weather3d_cover> covers; // weather3d_cover_set, at most 16
  texture_handle flakes{};             // weather3d_snow_flakes_set: none is the soft dots
  i32 flake_columns = 1, flake_rows = 1;
  f32 flake_sparkle = 0.0f;
  // bgfx rain/snow (njin_world3d_portable.cpp): set by draw_sky3d, drawn by end_3d.
  bool portable_precip = false;
  RenderTargetHandle portable_cover;  // top-down caster depth for weather3d::cover_auto
  RenderMeshHandle portable_drop_quad;
  std::shared_ptr<std::vector<RenderInstance>> portable_drops; // per-drop random numbers
  std::shared_ptr<const RenderCoverData> portable_frame_cover; // this pass's cover
  RenderMeshHandle portable_blade;
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

// The blades of one grass cell: grass_blade_floats each (position, height;
// facing, width, shade, phase; tint, three spare). Empty when nothing grows there.
void grass_cell_blades(const terrain3d_slot &t, const grass3d_slot &g, i32 cx, i32 cz, std::vector<f32> &out);

// Gerstner displacement and normal at rest position (x, z) and time t, the
// same sums the water shader makes.
void water_displace(const water3d_desc &w, f32 x, f32 z, f32 t, vec3 &offset, vec3 &normal);
// The same, drawn by vertices `spacing` metres apart: a wave fades out between four and
// two vertices per wavelength, so a coarse far mesh shows no aliasing (0: every wave).
void water_displace(const water3d_desc &w, f32 x, f32 z, f32 t, vec3 &offset, vec3 &normal, f32 spacing);

sky_params sky_params_of(const sky3d &sky);
sky_params sky_params_from_light(const context &ctx);

// Buoyancy: the surface under `at` for physics3d_step (njin_physics3d_impl.h).
bool world3d_water_surface(const context &ctx, u32 water, vec3 at, vec3 &point, vec3 &normal);

// GPU side (modules/world3d_draw.cpp).
void world3d_free_terrain(terrain3d_slot &t);
void world3d_free_grass(grass3d_slot &g);

// Core module: advances the waves' clock.
mod_desc world3d_module();
void portable_terrain_draw(const context &,terrain3d_handle);
void portable_terrain_release(context &,terrain3d_slot &);
void portable_grass_draw(const context &,grass3d_handle);
void portable_water_draw(const context &,water3d_handle);
void portable_scatter_draw(const context &,scatter3d_handle);
struct model_store;
// The draw_sky3d() sky of this pass (if any), drawn at the far plane by end_3d.
void portable_sky_draw(context &,model_store &);
// draw_sky3d() rain/snow of this pass (if any), after the opaque draws.
void portable_precip_draw(context &,model_store &);
// The weather cover of this pass (wetness or rain/snow), before the opaque draws.
std::shared_ptr<const RenderCoverData> portable_weather_cover(context &,model_store &);
} // namespace njin
