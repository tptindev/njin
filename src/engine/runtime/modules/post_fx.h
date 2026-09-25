#pragma once
#include "njin_post.h"
#include <raylib.h>

namespace njin {
struct njin_ctx;

// Built-in post-processing (njin_post.h), run by the camera module on the
// finished world image before the game's own post shader.
//
// Passes: optional blur (two separable passes at full size), optional bloom
// (bright pass, then blurred twice at half size), then one "uber" pass that
// does everything else and adds the bloom. Shaders and targets are created
// on first use and resized with the window.
struct post_chain {
  post_fx settings{};

  Shader bright{};
  Shader blur{};
  Shader uber{};
  bool loaded = false;
  bool failed = false;

  // Uniform locations.
  i32 bright_threshold = -1;
  i32 blur_direction = -1;
  i32 u_bloom_tex = -1, u_bloom = -1, u_resolution = -1, u_time = -1;
  i32 u_brightness = -1, u_contrast = -1, u_saturation = -1, u_sepia = -1,
      u_tint = -1;
  i32 u_vignette = -1, u_vignette_radius = -1, u_vignette_softness = -1,
      u_vignette_color = -1;
  i32 u_chromatic = -1, u_scanlines = -1, u_scanline_size = -1, u_curve = -1,
      u_pixelate = -1, u_grain = -1;

  RenderTexture2D full_a{}, full_b{}; // screen size
  RenderTexture2D half_a{}, half_b{}; // half screen size, for bloom

  post_chain() = default;
  ~post_chain();
  post_chain(const post_chain &) = delete;
  post_chain &operator=(const post_chain &) = delete;
};

// True when any built-in effect is on, so the world must be drawn into an
// offscreen target first.
bool post_chain_active(const post_chain &chain);

// Runs the enabled effects on `scene` (a render texture's colour buffer, the
// size of the screen). Returns the texture holding the result: `scene`
// itself when nothing ran. The result is stored bottom-up like any render
// texture, so draw it with a negative source height.
const Texture2D &post_chain_run(njin_ctx &ctx, const Texture2D &scene);
} // namespace njin
