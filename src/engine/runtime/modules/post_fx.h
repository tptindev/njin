#pragma once
#include "../njin_internal_only.h"
#include "njin_post.h"
#include <array>
#include <raylib.h>

namespace njin {
struct context;

// Built-in post-processing (njin_post.h), run by the camera module on the
// finished world image before the game's own post shader.
//
// Passes: optional blur (two separable passes, at half size when wide),
// optional depth of field (the image blurred at half size, mixed back by how
// far each pixel is from the focus), optional bloom
// (bright pass, then blurred twice at half size), then one "uber" pass that
// does everything else and adds the bloom. Shaders and targets are created
// on first use and resized with the window.
struct post_chain {
  post_fx settings{};

  Shader bright{};
  Shader blur{};
  Shader uber{};
  Shader dof{};
  bool loaded = false;
  bool failed = false;

  // Uniform locations.
  i32 bright_threshold = -1;
  i32 blur_direction = -1;
  i32 dof_blur = -1, dof_depth = -1, dof_planes = -1, dof_focus = -1;
  i32 dof_center = -1, dof_inv_vp = -1, dof_haze = -1;
  i32 u_bloom_tex = -1, u_bloom = -1, u_resolution = -1, u_time = -1;
  i32 u_brightness = -1, u_contrast = -1, u_saturation = -1, u_sepia = -1,
      u_tint = -1;
  i32 u_vignette = -1, u_vignette_radius = -1, u_vignette_softness = -1,
      u_vignette_color = -1;
  i32 u_chromatic = -1, u_scanlines = -1, u_scanline_size = -1, u_curve = -1,
      u_pixelate = -1, u_grain = -1;

  // The value last written to each uniform of `uber`, slot by location. A
  // program remembers its uniforms, so an unchanged one is not sent again.
  struct uniform_memory {
    i32 loc = -1;
    i32 count = 0;
    f32 value[4] = {};
  };
  std::array<uniform_memory, 32> uniforms{};

  RenderTexture2D full_a{}, full_b{}; // screen size
  RenderTexture2D half_a{}, half_b{}; // half screen size, for bloom

  post_chain() = default;
  ~post_chain();
  post_chain(const post_chain &) = delete;
  post_chain &operator=(const post_chain &) = delete;
};

// Compiles the three shaders now instead of when an effect is first switched
// on (a pause menu blur would stall the frame it opens on). Safe to call again.
void post_chain_warmup(context &ctx);

// True when any built-in effect is on, so the world must be drawn into an
// offscreen target first.
bool post_chain_active(const post_chain &chain);

// The depth buffer of the world image, for the depth of field: a depth
// texture the size of the scene, and the near and far planes of the 3D camera
// that wrote it. texture 0 = no 3D this frame, the depth of field is skipped.
struct post_depth {
  u32 texture = 0;
  f32 near_plane = 0.05f;
  f32 far_plane = 1000.0f;
  Matrix inv_view_proj{}; // back from window depth to world positions (dof_radius)
};

// Runs the enabled effects on `scene` (a render texture's colour buffer, the
// size of the screen). Returns the texture holding the result: `scene`
// itself when nothing ran. The result is stored bottom-up like any render
// texture, so draw it with a negative source height.
const Texture2D &post_chain_run(context &ctx, const Texture2D &scene, const post_depth &depth = {});
} // namespace njin
