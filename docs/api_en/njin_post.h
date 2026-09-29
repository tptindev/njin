#pragma once
#include "_types.h"

namespace njin {
struct context;

/// @addtogroup grp_post
/// @{

/// Built-in post-processing effects, applied to the world (not the UI).
///
/// Each effect is off at its default value; to enable one, adjust that
/// effect's fields. Set with post_fx_set(); it can be changed every frame (for
/// example, raise a red vignette as health gets low). Order of application:
/// blur, bloom, then a single pass with CRT curvature, pixelate, chromatic
/// aberration, color adjustment, scanlines, vignette, grain. If the game has
/// its own shader (camera_set_post_shader()), that shader runs **last**.
///
/// Bloom and blur cost a few extra full-screen passes; the remaining effects
/// are merged into one pass, so they are almost free.
struct post_fx {
  /// @name Color adjustment
  /// @{
  f32 brightness = 0.0f; ///< Added brightness, -1..1. 0 is off.
  f32 contrast = 1.0f;   ///< Contrast. 1 leaves it unchanged.
  f32 saturation = 1.0f; ///< Saturation. 0 is black and white, 1 leaves it unchanged.
  f32 sepia = 0.0f;      ///< Amount of old-photo tone, 0..1.
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Color multiplied into the whole frame.
  /// @}

  /// @name Vignette (darkened edges)
  /// @{
  f32 vignette = 0.0f;         ///< Strength, 0..1. 0 is off.
  f32 vignette_radius = 0.75f; ///< Radius of the bright area, measured from the center (0.7 is close to the edge).
  f32 vignette_softness = 0.45f; ///< Softness of the edge.
  rgba vignette_color{0.0f, 0.0f, 0.0f, 1.0f}; ///< Edge color. Red for a low-health effect.
  /// @}

  /// @name Bloom (glow)
  /// @{
  f32 bloom = 0.0f;           ///< Strength. 0 is off, 0.5–1.5 is typical.
  f32 bloom_threshold = 0.7f; ///< Only areas brighter than this level (0..1) glow.
  f32 bloom_radius = 1.0f;    ///< Spread. 1 is the default, 2 is twice as wide.
  /// @}

  /// @name Blur and old-machine looks
  /// @{
  f32 blur = 0.0f;       ///< Blurs the whole frame, radius in pixels. 0 is off. For the pause menu.
  f32 chromatic = 0.0f;  ///< Red/blue color separation at the edges, in pixels. 0 is off. 2–6 for a heavy hit.
  f32 scanlines = 0.0f;  ///< Strength of CRT-style horizontal stripes, 0..1. 0 is off.
  f32 scanline_size = 3.0f; ///< Distance between stripes, in screen pixels.
  f32 crt_curve = 0.0f;  ///< CRT-style screen curvature, 0..0.3. 0 is off.
  f32 pixelate = 0.0f;   ///< Size of the pixelation squares, in screen pixels. Below 2 is off.
  f32 grain = 0.0f;      ///< Film-style grain, 0..0.3. 0 is off.
  /// @}
};

/// Sets the built-in post-processing effects. `post_fx{}` turns everything off.
/// @param ctx Engine context.
/// @param fx The effects.
void post_fx_set(context &ctx, const post_fx &fx);

/// The effects currently set. Modify a copy, then call post_fx_set() again.
/// @param ctx Engine context.
/// @return The current effects.
post_fx post_fx_get(const context &ctx);

/// Interpolates between two effect sets, for smooth transitions (for example
/// into the pause menu).
/// @param a The set at `t = 0`.
/// @param b The set at `t = 1`.
/// @param t Progress, 0..1.
/// @return The effect set in between.
post_fx post_fx_lerp(const post_fx &a, const post_fx &b, f32 t);

/// Commonly used post_fx sets. They are plain values: modify them freely before setting.
///
/// @code
/// njin::post_fx fx = njin::post::crt();
/// fx.bloom = 0.6f;
/// njin::post_fx_set(ctx, fx);
/// @endcode
namespace post {
/// Old CRT screen: curved, scanlines, slight color separation, darkened edges.
/// @return The effect set.
inline post_fx crt() {
  post_fx p{};
  p.crt_curve = 0.12f;
  p.scanlines = 0.35f;
  p.chromatic = 1.5f;
  p.vignette = 0.45f;
  p.contrast = 1.1f;
  p.brightness = 0.03f;
  return p;
}

/// Black-and-white film: no color, high contrast, darkened edges, grain.
/// @return The effect set.
inline post_fx noir() {
  post_fx p{};
  p.saturation = 0.0f;
  p.contrast = 1.25f;
  p.vignette = 0.6f;
  p.vignette_radius = 0.65f;
  p.grain = 0.08f;
  return p;
}

/// Old photo: brown tone, slightly faded, darkened edges.
/// @return The effect set.
inline post_fx vintage() {
  post_fx p{};
  p.sepia = 0.8f;
  p.contrast = 0.9f;
  p.vignette = 0.5f;
  p.grain = 0.05f;
  return p;
}

/// Dreamy: wide glow, more vivid colors, light vignette.
/// @return The effect set.
inline post_fx dream() {
  post_fx p{};
  p.bloom = 0.9f;
  p.bloom_threshold = 0.55f;
  p.bloom_radius = 1.6f;
  p.saturation = 1.15f;
  p.vignette = 0.25f;
  return p;
}

/// Bright glow: bloom for neon, fire and magic games.
/// @return The effect set.
inline post_fx glow() {
  post_fx p{};
  p.bloom = 1.0f;
  p.bloom_threshold = 0.65f;
  return p;
}

/// Coarse pixels like an 8-bit machine, with light scanlines.
/// @return The effect set.
inline post_fx retro() {
  post_fx p{};
  p.pixelate = 4.0f;
  p.scanlines = 0.2f;
  p.scanline_size = 4.0f;
  p.saturation = 1.1f;
  return p;
}

/// Hurt or low health: red edges, slightly washed out. Scale `vignette` by the
/// amount of health lost to ramp it up gradually.
/// @return The effect set.
inline post_fx hurt() {
  post_fx p{};
  p.vignette = 0.7f;
  p.vignette_radius = 0.6f;
  p.vignette_color = {0.6f, 0.0f, 0.0f, 1.0f};
  p.saturation = 0.7f;
  return p;
}

/// Pause menu: the world is blurred and darkened behind the UI.
/// @return The effect set.
inline post_fx paused() {
  post_fx p{};
  p.blur = 6.0f;
  p.brightness = -0.15f;
  p.saturation = 0.6f;
  return p;
}
} // namespace post
/// @}
} // namespace njin
