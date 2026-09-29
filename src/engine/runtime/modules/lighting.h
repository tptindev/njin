#pragma once
#include "../njin_internal_only.h"
#include "njin_light.h"
#include <raylib.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace njin {
struct context;

// 2D lighting (njin_light.h), run by the camera module on the finished world
// image, before the built-in post effects.
//
// Deferred PBR. The world image is the albedo. Sprites with a normal map, a
// material map (metallic, roughness, ambient occlusion, raylib's "MRA" packing)
// or an emissive map draw them into three small "G-buffer" images. The light passes then run in linear HDR (16-bit
// float) space: the target starts at ambient * albedo * occlusion, and each
// light adds its Cook-Torrance contribution (GGX distribution, Smith
// geometry, Fresnel-Schlick, energy conserving between the diffuse and the
// specular part) as one quad. A last pass applies the exposure, a filmic
// shoulder that keeps the low tones as they are, and the gamma back to sRGB.
// Shadows test the occluder edges near each light; the penumbra is worked out from
// how much of the light's disc each edge hides, not sampled.
//
// Shadows: the occluder edges that can shadow a light are sorted into 32 buckets
// (angular sectors around a point or spot light, strips across a directional
// one) and uploaded to a float texture once per frame, so a pixel tests only
// the few edges of its own bucket, not every edge near the light.
//
// Kept cheap the usual ways: lights and occluders outside the view are dropped;
// each light is one quad no bigger than its own reach (a cone gets the box of
// the cone), with pixels the light cannot reach discarded before any shadow
// work; a spatial grid finds the occluder edges near a light; each pixel goes
// through the edges of its bucket once; uniform locations are looked up once; buffers are reused between
// frames; nothing at all runs when there is no light and the ambient light
// leaves the world as it is. The images can also be smaller than the world
// image (lighting_desc::scale).

// Uniform locations, looked up once when the shaders load.
struct light_locations {
  // The light shader.
  i32 albedo = -1, normals = -1, material = -1, target_size = -1, use_normals = -1, use_material = -1;
  i32 kind = -1, falloff = -1, lpos = -1, ldir = -1, lcolor = -1, radius = -1, source_size = -1;
  i32 height = -1, elevation = -1, cone_outer = -1, cone_inner = -1, reach = -1;
  i32 edges = -1, bucket_base = -1, bucket_count = -1, has_shadows = -1, bin_axis = -1, bin_lo = -1, bin_inv = -1;
  // The ambient shader.
  i32 ambient_material = -1, ambient_emissive = -1, ambient_size = -1, ambient_use_material = -1;
  i32 ambient_use_emissive = -1, ambient_color = -1;
  // The final shader.
  i32 final_hdr = -1, final_size = -1, final_exposure = -1, final_tonemap = -1;
  // The light shader's pixel shadows.
  i32 pixel_shadows = -1, pixel_row = -1, pixel_columns = -1, pixel_inv = -1, pixel_off = -1, pixel_tol = -1;
  // The march shader.
  i32 march_occluders = -1, march_map_size = -1, march_mode = -1, march_columns = -1, march_alpha = -1, march_origin = -1;
  i32 march_max_len = -1, march_rot = -1, march_strip0 = -1, march_strip_step = -1, march_dir = -1, march_first_row = -1;
};

// The outline of one frame of a sprite (light_occluder_sprite), in the pixels of
// the frame: closed loops, the ones around holes marked.
struct silhouette_loop {
  std::vector<vec2> points;
  bool hole = false;
};

struct lighting_state {
  lighting_desc desc{};
  light_locations loc{};

  // The textures of pixel occluders already set to clamp: with the default repeat, the bottom row of a
  // picture shows above its top edge where the quad's edge falls between two texels.
  std::unordered_set<u32> clamped;
  i32 pixel_columns_used = 1024; // the columns of the pixel shadow map this frame (lighting_desc::shadow_columns, or automatic)

  // Outlines traced from sprite frames, by frame (texture, rectangle, settings).
  std::unordered_map<u64, std::vector<silhouette_loop>> silhouettes;
  // The pixels of the textures those were traced from, read back once, by GL
  // texture id. `version` is the texture's own (it moves on when reloaded).
  struct cpu_image {
    Image image{};
    u32 version = 0;
  };
  std::unordered_map<u32, cpu_image> images;

  Shader light{};
  Shader ambient{};
  Shader final_pass{};
  Shader march{};            // marches one light's 1D shadow map through the occluder map
  bool loaded = false;
  bool failed = false;
  bool hdr_ok = true; // false when the machine cannot render to a float texture

  RenderTexture2D hdr{};      // the linear light, at lighting_desc::scale of the world image
  RenderTexture2D normals{};  // G-buffers, the same size
  RenderTexture2D materials{}; // metallic, roughness, occlusion
  RenderTexture2D emissives{}; // what glows, as the picture shows it
  RenderTexture2D lit{};      // the finished image, the size of the world image
  Texture2D edge_texture{};   // the occluder edges of each light, in buckets (float RGBA)
  RenderTexture2D occluder_map{}; // the alpha of what casts pixel-perfect shadows: the view and a margin
  RenderTexture2D shadow_rows{};  // a row per light: where the first occluder starts and ends, per angle

  lighting_state() = default;
  ~lighting_state();
  lighting_state(const lighting_state &) = delete;
  lighting_state &operator=(const lighting_state &) = delete;
};

// True when lighting is on, so the world must be drawn into an offscreen target.
bool lighting_active(const lighting_state &state);

// Lights `scene` (the world image, a render texture's colour buffer) as seen
// through `camera`, the one the world was drawn with. Returns the texture with
// the result, or `scene` itself when lighting is off or failed. Stored
// bottom-up like any render texture.
const Texture2D &lighting_apply(context &ctx, const Camera2D &camera, const Texture2D &scene);
} // namespace njin
