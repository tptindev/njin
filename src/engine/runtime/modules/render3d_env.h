#pragma once
#include "../njin_internal_only.h"
#include "njin_3d.h"
#include <raylib.h>

// light3d::environment, made ready to light PBR materials (render3d.cpp's
// pbr_light). From the game's equirectangular image njin makes, once per image:
//   - `base`: the image in linear light (an 8-bit image is taken as sRGB and
//     decoded, an .hdr one is linear already), 1024 x 512, mipmapped. The sky
//     (light3d::environment_visible) and the prefilter read it.
//   - `sh`: its irradiance as nine spherical harmonics coefficients, from a
//     64 x 32 copy read back to the CPU.
//   - `atlas`: its radiance prefiltered with the GGX distribution for
//     `levels` roughnesses (0, 0.2 ... 1), each an equirectangular image of
//     512 x 256, stacked from the top (roughness 0), half floats.
// The shaders add the split-sum's scale and bias analytically (env_brdf), so
// there is no lookup texture.
namespace njin {
struct context;

// The texture unit of `atlas` during a pass: past the material maps DrawMesh
// binds (0..10), below the shadow maps (14, 15). A game shader's textures use
// 10..13, but a game shader replaces the built-in one, which alone reads it.
constexpr i32 env_unit = 11;

// An sRGB colour's rgb in linear light (light3d::tonemap works there).
struct rgb_linear {
  f32 r = 0.0f, g = 0.0f, b = 0.0f;
};
rgb_linear linear_of(rgba c);

struct env_maps {
  u32 source = 0;  // texture slot id of the image the maps were made from (0 none)
  u32 version = 0; // and its version then
  bool ready = false;
  bool warned = false; // a failure was logged for this image
  Texture2D base{};
  Texture2D atlas{};
  i32 levels = 6;
  i32 level_height = 256;
  vec3 sh[9]{};
  // Made on first use.
  bool shaders_tried = false;
  Shader copy{}, prefilter{}, sky{};
  i32 sky_env_params = -1, sky_tone = -1, sky_exposure = -1, sky_inv_vp = -1, sky_eye = -1, sky_map = -1;
};

// Makes the maps when light3d::environment names an image they were not made
// from (or it changed). Draws into its own framebuffers: call it outside a
// pass's drawing, then bind the pass's target again. True when it drew.
bool env_update(context &ctx, env_maps &env, const light3d &light);

// The environment uniforms of a lighting shader: off when there is no ready map.
void env_set_uniforms(const env_maps &env, const light3d &light, Shader shader, i32 on, i32 sh, i32 map,
                      i32 params);

// Binds `atlas` to env_unit for the pass (nothing when not ready).
void env_bind(const env_maps &env);

// Fills the pass's background with the environment, tone mapped as the scene.
void env_draw_sky(env_maps &env, const light3d &light, const camera3d &camera, const Matrix &view_proj);

void env_free(env_maps &env);
} // namespace njin
