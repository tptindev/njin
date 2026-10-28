#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"
#include "debug3d.h"
#include "njin_3d.h"
#include "render3d_cull.h"
#include "render3d_env.h"
#include "render3d_ext.h"
#include <raylib.h>
#include <array>
#include <unordered_map>
#include <vector>

namespace njin {
struct context;

// The 3D pass (njin_3d.h): begin_3d/end_3d open it inside the world pass of
// the camera module, which closes one the game left open (render3d_close).
//
// Draw calls are recorded, not drawn: each keeps its mesh or model, transform,
// colour, the game shader bound at the time (shader_begin) and the fx3d and
// material3d state. end_3d then
//   1. renders the depth of every opaque, shadow-casting draw from the sun
//      into the shadow map (when light3d::shadows is on),
//   2. binds the world image again and draws every call in order through the
//      built-in shader (the SDF shader for draw_shape3d, which shares its
//      lighting code and sphere-traces the shape inside its box): Lambert + Blinn-Phong from the sun and up to
//      light3d_max point/spot lights, ambient, the sun's shadow (3x3 PCF),
//      normal and emission maps, emission, flash, dissolve and fog,
//   3. draws the 3D particles (particles3d.h).
// A game shader replaces the built-in one for its draws and gets the sun,
// ambient and eye position under the built-in names.
//
// camera_shake turns the view in begin_3d. The shaders, the primitive meshes
// and the shadow map are made on first use, so a 2D game never pays for them.

// Uniform locations of the built-in shader, looked up once when it loads.
struct render3d_locations {
  i32 light_dir = -1, light_color = -1, ambient = -1, view_pos = -1;
  i32 fog_color = -1, fog_density = -1;
  i32 light_count = -1, light_pos = -1, light_colors = -1, light_spot = -1;
  i32 surface = -1, emission = -1, rim = -1, emission_color = -1, unlit = -1;
  i32 use_normal_map = -1, use_emission_map = -1, world_uv = -1, under_amount = -1, use_under_normal = -1;
  i32 alpha_cutoff = -1, snow_cover = -1;
  i32 shadow_on = -1, shadow_map = -1, light_vp = -1, shadow_params = -1;
  i32 flash = -1, dissolve = -1, edge_color = -1, view_mask = -1;
  // The SDF shader only.
  i32 clay_surface = -1;
  i32 shape_kind = -1, shape_dims = -1, shape_bounds = -1, shape_to_local = -1, shape_to_world = -1;
  i32 mat_vp = -1, ray_ortho = -1, ray_dir = -1, depth_only = -1;
  i32 blend_a = -1, blend_b = -1, blend_count = -1, blend_k = -1; // draw_sdf_blend
  // Point/spot light shadows, and the skinning shader's bones.
  i32 light_shadow = -1, lamp_vp = -1, lamp_params = -1, lamp_map = -1, lamp_rows = -1;
  i32 bones = -1;
  // HDR, cascades, the extended lights, probes, the irradiance volume, the
  // material extensions, transmission and OIT (render3d_ext.h).
  i32 hdr_out = -1, shadow_cascades = -1, cascade_vp = -1, cascade_split = -1, cascade_texel = -1, cascade_span = -1;
  i32 view_dir = -1, cluster_on = -1;
  i32 pbr_extra = -1, transmit = -1, attenuation_color = -1, scene_on = -1, scene_color = -1, scene_vp = -1;
  i32 height_scale = -1, lightmap_params = -1, light_map = -1, oit_pass = -1;
  // Tone mapping, PBR and the environment (light3d::tonemap, material3d::pbr,
  // light3d::environment).
  i32 tone_map = -1, exposure = -1, light_color_linear = -1, ambient_linear = -1, light_colors_linear = -1;
  i32 emission_linear = -1, pbr_params = -1, use_pbr_maps = -1;
  i32 env_on = -1, env_sh = -1, env_map = -1, env_params = -1;
  // Ray traced shadows (render3d_rt.cpp).
  i32 rt_on = -1, rt_mask = -1, rt_refl = -1, rt_refl_tex = -1;
};

// A draw of the outdoor world (njin_world3d.h), made by world3d_draw.cpp.
enum world3d_kind : u8 {
  world3d_none = 0,
  world3d_terrain, // opaque, casts shadows
  world3d_grass,   // opaque, casts shadows when grass3d_desc::cast_shadows
  world3d_sky,     // behind everything: where nothing was drawn
  world3d_water,   // see-through: in the translucent pass
  world3d_precip,  // see-through
};

// One recorded draw: a primitive (mesh), an SDF shape, a model, or a piece of
// the outdoor world (`world`, with its handle id in `world_id`).
struct draw3d_cmd {
  u8 world = world3d_none;
  u32 world_id = 0;
  bool is_shape = false;
  shape3d shape;
  const Mesh *mesh = nullptr;
  model_handle model{};
  Matrix transform{};
  rgba color{};           // primitive colour, or model tint
  shader_handle shader{}; // bound by the game when recorded; 0 = built-in
  fx3d fx;
  material3d material;    // primitives only; a model has its own
  // draw_instanced3d: the buffer (id 0 = not instanced) and its range.
  instance_buffer_handle buffer{};
  u32 first = 0;
  u32 count = 0;
  // draw_model_anim: the pose's matrices in render3d_state::bones. count 0 =
  // the rest pose, drawn without skinning.
  u32 bone_first = 0;
  u32 bone_count = 0;
  // draw_sdf_blend: its parts in render3d_state::blend_parts (count 0 = a
  // single shape3d), and how soft the joints are.
  u32 blend_first = 0;
  u32 blend_count = 0;
  f32 blend_k = 0.0f;
  // Models: the level of detail drawn (0 = the full mesh, model_lod_build()).
  // Models, primitives and SDF shapes: whether the box is outside the camera's
  // frustum. A draw out of view still casts its shadow into it, so only the
  // camera pass skips it.
  u8 lod = 0;
  bool culled = false;
  // Hidden behind other draws, seen from the camera (render3d_options::occlusion,
  // hiz_mark()). Like `culled`, only the camera pass skips it.
  bool occluded = false;
  // draw_instanced3d with an instanced3d_desc: cull each instance, pick each
  // one's level of detail, and the radius round an instance's position at
  // scale 1 (0: from the bounds). See instances_select().
  bool inst_cull = false;
  bool inst_lod = false;
  f32 inst_radius = 0.0f;
  // World-space box round the draw (centre and half size), when `boxed`, for
  // culling against the camera and each shadow pass (cull_boxes()).
  // Instanced and world3d draws have none: they cull themselves, or not.
  bool boxed = false;
  vec3 box_center{};
  vec3 box_extent{};
  // Models: materials drawn in another colour than their own (draw_model_anim
  // with model_recolor), in render3d_state::recolors. count 0 = none.
  u32 recolor_first = 0;
  u32 recolor_count = 0;
  // Models with morph targets: the weight of each morph in
  // render3d_state::morphs (count 0 = the model has none).
  u32 morph_first = 0;
  u32 morph_count = 0;
  // Which draw of the last frame this one is, for post3d's motion vectors (0 =
  // none): the entity, the game's draw3d_motion_id(), or the mesh or model and
  // how many draws of it came before in the pass.
  u64 motion_key = 0;
};

// A draw of the last frame's motion pass (render3d_motion_commit): where it was,
// and its bone matrices in render3d_state::motion_bones.
struct motion_prev {
  Matrix transform{};
  u32 bone_first = 0;
  u32 bone_count = 0;
};

// Depth seen from the sun. A colour attachment is kept too, so the
// framebuffer is complete on every GL 3.3 driver.
struct shadow_target {
  u32 fbo = 0;
  u32 depth = 0;
  u32 color = 0;
  i32 size = 0;   // width
  i32 height = 0;
};

// The shadowed point and spot lights of the open pass (light3d_source::shadows):
// row of the lamp atlas per light (-1 none), and per row the face matrices and
// the lighting shader's lampParams. Rows past light3d_shadow_max and lights
// past light3d_max only with the extended lights (light3d::source_shadows,
// lights_max), which read them from textures.
struct lamp_shadows {
  std::array<i32, light3d_limit> row{};
  std::array<Matrix, light3d_shadow_limit * 6> vp{};
  std::array<vec4, light3d_shadow_limit> params{};
  i32 rows = 0;
};

// Copies the open pass for the inspector. Called by end_3d before drawing.
void render3d_capture_debug(context &ctx);

struct render3d_state {
  bool ready = false;  // shaders and meshes created
  bool failed = false; // creating them failed once: do not retry every frame
  bool active = false; // between begin_3d and end_3d
  // end_3d's second pass: only the model parts whose material is see-through
  // (alpha below 1: glTF transmission), after every opaque one, depth not written.
  bool translucent_pass = false;
  Shader lit{};
  Shader sdf{};        // draw_shape3d, both passes
  Shader depth{};      // shadow pass, meshes
  Shader lit_instanced{};   // draw_instanced3d
  Shader depth_instanced{}; // draw_instanced3d, shadow pass
  Shader lit_skinned{};     // draw_model_anim
  Shader depth_skinned{};   // draw_model_anim, shadow passes
  bool skin_ok = false;     // the skinning shaders compiled
  i32 depth_skinned_bones = -1;
  render3d_locations locs;
  render3d_locations sdf_locs;
  render3d_locations instanced_locs;
  render3d_locations skinned_locs;
  // The other variant of the four lighting shaders above and their locations
  // (render3d.cpp's use_ext): the extended one (NJIN_EXT: the light clusters,
  // probes, the irradiance volume, transmission) while the plain one is in use,
  // and the plain one while the extended one is. Made the first time a pass
  // needs it; a driver that cannot build it (macOS: 16 texture units) keeps the
  // plain shaders and those parts are off.
  Shader lit_alt{}, sdf_alt{}, lit_instanced_alt{}, lit_skinned_alt{};
  render3d_locations locs_alt, sdf_locs_alt, instanced_locs_alt, skinned_locs_alt;
  bool skin_ok_alt = false;
  bool ext_tried = false, ext_ok = false;
  bool ext_active = false; // the extended variant is the one in use
  Mesh cube{};     // 1x1x1, centred on the origin
  Mesh sphere{};   // radius 1
  Mesh plane{};    // 1x1 on xz, facing +y
  Mesh cylinder{}; // radius 1, from y = 0 to y = 1
  // The same two with few faces, for crowds drawn instanced (mesh3d_*_low).
  Mesh sphere_low{};
  Mesh cylinder_low{};
  // Default material maps. Ours, not raylib-allocated, so there is no
  // UnloadMaterial (which would also free the shared shader).
  std::array<MaterialMap, 12> maps{};
  light3d light;
  shadow_target shadow;
  shadow_target lamp;    // atlas of the point/spot light shadows
  lamp_shadows lamps;
  // Rain and snow cover: the depth of what stands above the ground, seen
  // straight down round the camera (weather3d::cover_auto). world3d_draw.cpp
  // hides drops under it and keeps the ground under it dry.
  shadow_target cover;
  Matrix cover_vp{};
  bool cover_drawn = false; // the map holds the casters round this pass's camera
  vec2 cover_at{};          // camera xz the map was drawn round
  i32 cover_age = -1;       // passes since it was drawn; -1 none yet

  // The open pass. Draw calls take a const ctx, so the list is mutable.
  camera3d camera; // shake included
  // Sub-pixel shift of the open world pass's projection, in NDC, while TAA
  // (post3d) resolves it; zero otherwise. load_camera() applies it.
  vec2 jitter{};
  // The open pass's frustum, as planes (xyz the inward normal, w the offset:
  // a point p is inside when dot(xyz, p) + w >= 0 for all six), and what
  // model LOD picking needs: tan(fovy / 2).
  std::array<vec4, 6> frustum{};
  f32 tan_half_fovy = 1.0f;
  mutable std::vector<draw3d_cmd> cmds;
  mutable std::vector<Matrix> bones; // bone matrices of the posed draws
  mutable std::vector<model_recolor> recolors; // materials recoloured by the draws
  mutable std::vector<f32> morphs;             // morph weights of the draws
  mutable std::vector<sdf_part> blend_parts; // parts of the blended SDF shapes
  std::vector<light3d_source> lights;
  bool entities = true; // camera3d::entities of the open pass
  // The open pass draws into a render texture (begin_3d with a target)
  // instead of the world target: its framebuffer, 0 for none, and size.
  u32 target_fbo = 0;
  vec2 target_size{};
  fx3d fx;
  material3d material;
  debug3d_frame debug; // the last pass, while the debug server runs
  // The depth the last pass left in the world target, for the depth of field
  // of post_fx: its camera's planes, and whether a pass drew since the post
  // chain last read them.
  f32 depth_near = 0.05f, depth_far = 1000.0f;
  Matrix depth_inv_view_proj{}; // window depth back to world positions
  bool depth_drawn = false;
  // The reflecting surfaces of post3d::ssr (render3d_draw_reflectors), made on
  // first use: the depth programs with a fragment shader that writes the
  // surface's material3d::reflect where it is the nearest surface.
  Shader mask{}, mask_instanced{}, mask_skinned{};
  i32 mask_skinned_bones = -1;
  bool mask_ready = false;
  bool mask_failed = false;
  // Motion vectors for post3d's TAA and motion blur (render3d_draw_velocity):
  // the next draw's draw3d_motion_id(), the draws seen so far this pass per
  // mesh or model, and where each keyed draw was in the last motion pass.
  mutable u64 next_motion_id = 0;
  mutable std::unordered_map<u64, u32> motion_seen;
  std::unordered_map<u64, motion_prev> motion_last;
  std::vector<Matrix> motion_bones;
  Shader velocity{}, velocity_skinned{};
  u32 prev_bones_tex = 0; // last frame's bone matrices of one draw, 4 texels each
  bool velocity_ready = false;
  bool velocity_failed = false;
  // light3d::environment, ready to light PBR materials (render3d_env.cpp).
  env_maps env;
  // light3d::shadow_cascades: how many were drawn this pass (1 = the single
  // map, as always), and each one's matrix, far distance, texel size and depth span.
  i32 cascades = 1;
  std::array<Matrix, 4> cascade_vp{};
  vec4 cascade_split{}, cascade_texel{}, cascade_span{};
  // The largest each cascade's sphere has been since the cascades last changed
  // (cascade_key: count, far distance, split, aspect): a field of view that
  // eases back with speed leaves the texels as they were.
  std::array<f32, 4> cascade_radius{};
  vec4 cascade_key{};
  // The opt-in parts (render3d_ext.h).
  hdr_state hdr;
  lights_ext lext;
  probe_state probes;
  volume_state volume;
  oit_state oit;
  rt_state rt;
  // Lit colours are written for the HDR image (or a probe's capture): the
  // shaders' hdrOut.
  bool hdr_out = false;
  // A probe or grid capture is drawing: no culling against the camera, no
  // probes in the lighting, every extended light.
  bool capturing = false;
  // post3d::oit for the open pass; which OIT pass is drawing (1, 2; 0 none),
  // and whether its draws are done (the translucent pass then skips them).
  bool oit_on = false;
  i32 oit_pass = 0;
  bool oit_done = false;
  // material3d::transmission: some draw of the last world pass had it (so the
  // next asks post3d for the scene's copy); the copy bound for this pass.
  bool transmission_seen = false;
  bool transmission_now = false; // the open pass has such a draw
  bool scene_bound = false;
  Matrix scene_vp{};

  // render3d_options_set(), and what it needs: occlusion and the scratch
  // instance buffers (render3d_cull.h), and the programs that draw a batch of
  // draws with one matrix per instance (render3d_options::batch), made on
  // first use: lit, its extended variant, and depth.
  render3d_options options;
  cull3d_state cull;
  Shader lit_batched{}, lit_batched_ext{}, depth_batched{};
  render3d_locations batched_locs, batched_locs_ext;
  bool batch_tried = false, batch_ok = false, batch_ext_tried = false, batch_ext_ok = false;

  render3d_state() = default;
  ~render3d_state();
  render3d_state(const render3d_state &) = delete;
  render3d_state &operator=(const render3d_state &) = delete;
};

// Shape space of an SDF draw (rotation then position, no scale), the half size
// of the box it fits in, and the distance parameters of its kind (the SDF
// shader's shapeDims). Shared with ray3d_shape (njin_ray3d.cpp).
struct shape_frame {
  Matrix to_world;
  vec3 bounds;
  vec4 dims;
};
shape_frame shape3d_frame(const shape3d &shape);

// The near and far planes as the depth shaders read them (`planes`): the near
// plane is negated for an orthographic camera, whose depth is linear.
vec2 render3d_depth_planes(const camera3d &camera);

// Where the full-screen passes put the eye: the camera's position, or for an
// orthographic camera a point far back along the view, so the direction from
// the eye to any point is the camera's forward.
vec3 render3d_post_eye(const camera3d &camera);

// Closes a 3D pass the game left open. Called at the end of the world pass.
void render3d_close(context &ctx);

// The exposure lit colours are written with in the open pass (light3d::exposure,
// times the auto exposure into the HDR image; 1 in a probe's capture).
f32 render3d_out_exposure(const render3d_state &s);

// post3d: whether the next world pass wants the scene's colour and depth
// copied (a draw that lets light through, volumetric fog, HDR), and the AA.
bool render3d_wants_scene(const context &ctx);

// For the outdoor world (world3d_draw.cpp), which draws with the same lighting:
// the lighting GLSL (the sun and its shadow, lamps, fog: shade()), the uniform
// locations, the per-pass and per-draw uniforms, and recording its draws.
const char *render3d_lighting_glsl();
render3d_locations render3d_find_locations(Shader shader);
void render3d_set_pass_uniforms(const render3d_state &s, Shader sh, const render3d_locations &l, bool shadows,
                                const Matrix &light_vp, bool lamps);
void render3d_set_draw_uniforms(Shader sh, const render3d_locations &l, const material3d &m);
void render3d_record_world(const context &ctx, u8 kind, u32 id);
// Whether the box [lo, hi] is at least partly inside the open pass's frustum.
bool render3d_box_visible(const render3d_state &s, vec3 lo, vec3 hi);

// post3d (SSR): draws the open pass's opaque draws whose surface reflects
// (material3d::reflect > 0) into the bound target through the current
// matrices, each pixel getting its reflect in red, kept only where it is the
// nearest surface of `depth` (a copy of the pass's depth, `size` pixels; the
// pass's near and far planes in `planes`). Primitives, models (posed too) and
// draw_instanced3d; not SDF shapes, the outdoor world, translucent or
// dissolving draws. False when the programs cannot be made.
bool render3d_draw_reflectors(context &ctx, u32 depth, vec2 size, vec2 planes);

// post3d (TAA, motion blur): draws the open pass's opaque meshes and models
// into the bound target through the current matrices, each pixel where it is
// the nearest surface of `depth` getting (screen motion since the last motion
// pass in uv, that point's depth then, 1): from `prev_vp` and the draw's last
// transform and bones to `cur_vp` and its transform now. With `has_last` false,
// or for a draw not in the last pass, only the camera moved. Other pixels keep
// the clear. Not SDF shapes, draw_instanced3d, the outdoor world, translucent
// or dissolving draws. False when the programs cannot be made.
bool render3d_draw_velocity(context &ctx, u32 depth, vec2 size, vec2 planes, const Matrix &cur_vp,
                            const Matrix &prev_vp, bool has_last);
// Keeps where each keyed draw of the open pass is, for the next motion pass.
void render3d_motion_commit(context &ctx);

// world3d_draw.cpp: before the pass's draws (its shaders' pass uniforms),
// one recorded draw (the camera pass, or a shadow pass's depth seen through
// `view_proj`), and after them.
void world3d_pass_uniforms(context &ctx, bool shadows, const Matrix &light_vp, bool lamps);
// Puts the extended variant (NJIN_EXT) of the terrain, grass and water shaders
// in use for the open pass, or the plain one back (render3d.cpp's use_ext).
void world3d_use_ext(context &ctx, bool on);
void world3d_draw(context &ctx, const draw3d_cmd &c, const Matrix &view_proj);
void world3d_draw_depth(context &ctx, const draw3d_cmd &c, const Matrix &view_proj);
void world3d_pass_end(context &ctx);

// Core module: advances the pose of every njin::model3d in phase_update.
mod_desc render3d_module();
} // namespace njin
