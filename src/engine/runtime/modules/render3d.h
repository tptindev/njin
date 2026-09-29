#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"
#include "njin_3d.h"
#include <raylib.h>
#include <array>
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
  i32 use_normal_map = -1, use_emission_map = -1;
  i32 shadow_on = -1, shadow_map = -1, light_vp = -1, shadow_params = -1;
  i32 flash = -1, dissolve = -1, edge_color = -1;
  // The SDF shader only.
  i32 shape_kind = -1, shape_dims = -1, shape_bounds = -1, shape_to_local = -1, shape_to_world = -1;
  i32 mat_vp = -1, ray_ortho = -1, ray_dir = -1, depth_only = -1;
  // Point/spot light shadows, and the skinning shader's bones.
  i32 light_shadow = -1, lamp_vp = -1, lamp_params = -1, lamp_map = -1;
  i32 bones = -1;
};

// One recorded draw: a primitive (mesh), an SDF shape or a model.
struct draw3d_cmd {
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
// the lighting shader's lampParams.
struct lamp_shadows {
  std::array<i32, light3d_max> row{};
  std::array<Matrix, light3d_shadow_max * 6> vp{};
  std::array<vec4, light3d_shadow_max> params{};
  i32 rows = 0;
};

// What the inspector's 3D world view shows of the last pass (debug.cpp): one
// point per draw (per instance for draw_instanced3d), where it is and in its
// colour. Kept only while the debug server runs.
struct debug3d_item {
  i32 kind = 0; // debug3d_kind
  vec3 pos{};
  rgba color{};
};

// Kinds of debug3d_item; the inspector (panel_world.cpp) reads the same numbers.
enum debug3d_kind {
  debug3d_cube = 0,
  debug3d_sphere = 1,
  debug3d_plane = 2,
  debug3d_cylinder = 3,
  debug3d_model = 4, // the centre of the model's bounding box
  debug3d_shape = 10, // + shape3d_kind
};

struct debug3d_frame {
  f32 time = -1.0f; // elapsed() when captured; old frames are not sent
  camera3d camera;
  vec3 sun{};
  std::vector<light3d_source> lights;
  std::vector<debug3d_item> items;
  u32 instanced = 0; // instances drawn by draw_instanced3d (their positions stay on the GPU)
};

// Copies the open pass for the inspector. Called by end_3d before drawing.
void render3d_capture_debug(context &ctx);

struct render3d_state {
  bool ready = false;  // shaders and meshes created
  bool failed = false; // creating them failed once: do not retry every frame
  bool active = false; // between begin_3d and end_3d
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
  Mesh cube{};     // 1x1x1, centred on the origin
  Mesh sphere{};   // radius 1
  Mesh plane{};    // 1x1 on xz, facing +y
  Mesh cylinder{}; // radius 1, from y = 0 to y = 1
  // Default material maps. Ours, not raylib-allocated, so there is no
  // UnloadMaterial (which would also free the shared shader).
  std::array<MaterialMap, 12> maps{};
  light3d light;
  shadow_target shadow;
  shadow_target lamp;    // atlas of the point/spot light shadows
  lamp_shadows lamps;

  // The open pass. Draw calls take a const ctx, so the list is mutable.
  camera3d camera; // shake included
  mutable std::vector<draw3d_cmd> cmds;
  mutable std::vector<Matrix> bones; // bone matrices of the posed draws
  std::vector<light3d_source> lights;
  bool entities = true; // camera3d::entities of the open pass
  fx3d fx;
  material3d material;
  debug3d_frame debug; // the last pass, while the debug server runs

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

// Closes a 3D pass the game left open. Called at the end of the world pass.
void render3d_close(context &ctx);

// Core module: advances the pose of every njin::model3d in phase_update.
mod_desc render3d_module();
} // namespace njin
