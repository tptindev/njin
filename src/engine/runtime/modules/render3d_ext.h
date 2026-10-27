#pragma once
#include "../njin_internal_only.h"
#include "njin_3d.h"
#include "render3d_env.h"
#include <raylib.h>
#include <array>
#include <initializer_list>
#include <utility>
#include <vector>

// The parts of the 3D pass that are opt-in and need more than the classic
// path: the HDR image and what reads it (render3d_hdr.cpp), the extended
// lights (render3d_lights.cpp), reflection probes and the irradiance volume
// (render3d_probe.cpp), volumetric fog (render3d_volume.cpp). render3d.cpp
// drives them from end_3d; each keeps its state in render3d_state.
namespace njin {
struct context;
struct render3d_state;
struct render3d_locations;

// Texture units of the lighting shaders' extra maps, past the ones a draw
// binds (0..15, see render3d.cpp). A desktop GL driver has 32.
constexpr i32 light_tex_unit = 16;
constexpr i32 cluster_tex_unit = 17;
constexpr i32 index_tex_unit = 18;
constexpr i32 lamp_mat_unit = 19;
constexpr i32 probe_atlas_unit = 20;
constexpr i32 probe_sh_unit = 21;
constexpr i32 grid_tex_unit = 22;
constexpr i32 scene_color_unit = 23;
constexpr i32 lightmap_unit = 12;
// render3d.cpp's units of the sun's and the lamps' shadow maps.
constexpr i32 sun_shadow_unit = 14;
constexpr i32 lamp_shadow_unit = 15;

// A full-screen quad into the bound framebuffer's viewport through `shader`
// (texture coordinate 0 at the bottom left, identity matrices), `tex0` on
// unit 0 and up to four more textures by sampler name. The matrices are kept.
void render3d_quad(Shader shader, u32 tex0, std::initializer_list<std::pair<const char *, u32>> samplers = {});

// A colour texture of `format` (rlgl's pixel format) with a framebuffer, both
// freed by free_float_target.
bool make_float_target(i32 w, i32 h, i32 format, u32 &tex, u32 &fbo, bool linear = true);
void free_float_target(u32 &tex, u32 &fbo);

// ---------------------------------------------------------------------------
// HDR (light3d::hdr), bloom (post3d::bloom), auto exposure, and the
// image-space anti-aliasing (post3d::aa).
// ---------------------------------------------------------------------------
struct hdr_state {
  // The pass's HDR image, the world target's size: `fbo` with colour and
  // depth; `color_fbo` the colour alone.
  u32 fbo = 0, color_fbo = 0, color = 0, depth = 0;
  i32 w = 0, h = 0;
  bool active = false; // the open pass draws into it
  bool failed = false; // could not make it: passes draw as without HDR
  // Bloom: a chain of half-size images.
  std::vector<u32> mip_tex, mip_fbo;
  std::vector<i32> mip_w, mip_h;
  // Auto exposure: the mean log luminance, 64 x 64 down to 1 x 1, read back
  // at the next pass (a frame later, so the read does not wait on the GPU).
  std::array<u32, 7> lum_tex{}, lum_fbo{};
  f32 adapted = 1.0f;      // the auto exposure factor now
  f32 adapted_then = 1.0f; // the factor when the measure was taken
  bool measured = false;   // lum_tex[6] holds a measure not read yet
  bool adapting = false;   // auto exposure ran last pass
  f64 adapted_at = 0.0;
  // Anti-aliasing: a copy of the world image, SMAA's edges and weights, its
  // two lookup images.
  u32 aa_tex = 0, aa_fbo = 0, edges_tex = 0, edges_fbo = 0, weights_tex = 0, weights_fbo = 0;
  u32 area_tex = 0, search_tex = 0;
  i32 aa_w = 0, aa_h = 0;
  bool aa_claimed = false; // this frame's world image is anti-aliased
  bool loaded = false, load_failed = false;
  Shader init{}, resolve{}, down{}, up{}, lum{}, lum_down{}, fxaa{}, smaa_edge{}, smaa_weight{}, smaa_blend{};
  bool smaa_ok = false;
};
void hdr_free(hdr_state &h);

// Whether light3d::hdr asks for HDR (a tone curve set).
bool hdr_wanted(const context &ctx);
// Opens the HDR image for the world pass about to draw: the world target's
// colour taken into it in linear light, its depth copied. Reads last pass's
// exposure measure. False when it cannot: the pass draws as before.
bool hdr_begin(context &ctx);
// Binds the HDR image for drawing (viewport its size).
void hdr_bind(context &ctx);
// The exposure lit colours are multiplied by: light3d::exposure, times the
// auto exposure while HDR is on.
f32 hdr_exposure(const context &ctx);
// Bloom, the exposure measure, the tone curve into the world target, the
// depth back. Leaves the world target bound, HDR closed.
void hdr_resolve(context &ctx);
// post3d::aa over the world target, once a frame. Leaves it bound.
void aa_apply(context &ctx);
// A new frame: its world image is not anti-aliased yet.
void hdr_frame_begin(context &ctx);

// ---------------------------------------------------------------------------
// The extended lights (light3d::lights_max past 16, source_shadows past 4).
// ---------------------------------------------------------------------------
struct lights_ext {
  bool on = false;  // the open pass lights through them
  i32 total = 0;    // lights in light_tex
  u32 light_tex = 0, cluster_tex = 0, index_tex = 0, lamp_mat_tex = 0;
  i32 light_rows = 0, index_rows = 0;
  i32 tiles_x = 16, tiles_y = 9, slices = 24;
  f32 tile_px = 1.0f;
  vec4 depth{}; // near, far, log(far / near), 1 orthographic
  std::vector<f32> light_data, cluster_data, index_data, mat_data;
  std::vector<i32> counts, offsets;
};
void lights_ext_free(lights_ext &l);
// Whether the light settings ask for the extended lights.
bool lights_ext_wanted(const light3d &light);
// Fills the light textures and the clusters of the open pass (its camera,
// `size` pixels), the lamp shadow rows from `row` and their face matrices.
void lights_ext_update(render3d_state &s, vec2 size);
// Binds the textures to their units.
void lights_ext_bind(const lights_ext &l);
// The cluster uniforms of a lighting shader; `tile_scale` scales the tile's
// pixel size (0.5 for a half-size pass).
void lights_ext_uniforms(const render3d_state &s, Shader sh, f32 tile_scale = 1.0f);

// ---------------------------------------------------------------------------
// Reflection probes and the irradiance volume.
// ---------------------------------------------------------------------------
struct probe_slot {
  probe3d_desc desc{};
  u32 gen = 0;
  bool alive = false;
  bool dirty = true;  // capture at the next pass
  bool ready = false; // its images are in the atlas
};

struct probe_state {
  std::vector<probe_slot> slots;
  // The probes' prefiltered images side by side (probe3d_max columns), and
  // their irradiance (9 x probe3d_max).
  u32 atlas = 0, atlas_fbo = 0, sh_tex = 0;
  std::array<std::array<vec3, 9>, probe3d_max> sh{};
  // The capture: six faces side by side, colour and depth.
  u32 cap_fbo = 0, cap_color = 0, cap_depth = 0;
  i32 cap_size = 0;
  // The faces turned into an equirectangular image.
  u32 equi_tex = 0, equi_fbo = 0;
  env_maps builder; // makes the maps of one probe at a time
  Shader equi{};
  bool tried = false, ok = false;
  // The irradiance volume: an ambient cube per point (6 rows), captured a few
  // points a frame.
  bool grid_set = false, grid_ready = false;
  irradiance3d_desc grid{};
  i32 grid_next = 0; // next point to capture
  std::vector<f32> grid_data;
  u32 grid_tex = 0;
  u32 small_fbo = 0, small_color = 0, small_depth = 0;
  i32 small_size = 0;
};
void probes_free(probe_state &p);
// The atlas column count and sizes the shaders' probeParams need.
constexpr i32 probe_column = 256; // width of one probe's image
constexpr i32 probe_level = 128;  // height of one roughness level
constexpr i32 probe_levels = 6;
// Probe and grid uniforms of a lighting shader (none while capturing).
void probes_uniforms(const render3d_state &s, Shader sh, bool capturing);
void probes_bind(const render3d_state &s);
// The faces of a capture: their look direction and up, in render3d's lamp order.
extern const vec3 cube_dirs[6];
extern const vec3 cube_ups[6];
// Turns the six faces in the capture target into the probe's maps and puts
// them in the atlas. False on failure.
bool probe_store(render3d_state &s, i32 index);
// Makes the capture targets for faces of `size` (small: for the grid).
bool probe_capture_target(probe_state &p, i32 size, bool small);
// The ambient cube of one grid point from its six faces, read back, into the
// grid's data; uploads the grid once it is complete.
void grid_store(probe_state &p, i32 point, i32 size);

// ---------------------------------------------------------------------------
// Volumetric fog (light3d::volume_*).
// ---------------------------------------------------------------------------
struct volume_state {
  bool tried = false, ok = false;
  Shader march{}, compose{};
  u32 tex = 0, fbo = 0;
  i32 w = 0, h = 0;
  u32 frame = 0;
};
void volume_free(volume_state &v);
// The fog of the open pass into its target, from the depth texture `depth`
// (w x h) of its opaque draws: marched at half size, then laid over.
void volume_run(context &ctx, u32 depth, i32 w, i32 h, bool shadows, const Matrix &light_vp, bool lamps,
                const Matrix &view_proj);

// ---------------------------------------------------------------------------
// Order-independent transparency (post3d::oit).
// ---------------------------------------------------------------------------
struct oit_state {
  u32 accum_tex = 0, reveal_tex = 0, accum_fbo = 0, reveal_fbo = 0;
  u32 accum_clear = 0, reveal_clear = 0; // the same colour without the depth, to clear it alone
  u32 depth = 0; // the depth texture both framebuffers test against
  i32 w = 0, h = 0;
  Shader compose{}, fill{};
  bool tried = false, ok = false;
};
void oit_free(oit_state &o);
// Prepares the two targets against `depth` (w x h) and clears them.
bool oit_begin(oit_state &o, u32 depth, i32 w, i32 h);
// Binds the weighted-sum (pass 1) or the revealage (pass 2) target.
void oit_bind(const oit_state &o, i32 pass);
// Lays the result over `target` (w x h).
void oit_compose(oit_state &o, u32 target);
} // namespace njin
