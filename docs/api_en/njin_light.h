#pragma once
#include "_tilemap.h"
#include "_types.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace njin {
// Opaque, see njin_ctx.h.
struct njin_ctx;

/// @addtogroup grp_light
/// @{

/// Light kind.
enum light_kind : i32 {
  light_point,       ///< Point light: shines evenly in every direction from `transform.pos`, fading with distance.
  light_spot,        ///< Spot light (flashlight, stage light): shines only within an angle around the `angle` direction.
  light_directional, ///< Directional light (sun, moon): lights the whole scene evenly from one direction, has no position.
};

/// Distance falloff curve of point and spot lights.
enum light_falloff : i32 {
  /// Physical: brightness drops with the inverse square of the distance,
  /// `1 / (1 + (d / size)^2)`, then is smoothly cut to 0 at `radius` so the light has
  /// a finite reach. `size` is the size of the light source: exactly the distance at which
  /// the brightness is halved. The core is harsh and the edge darkens quickly, like a real light.
  falloff_physical,
  falloff_linear, ///< Linear falloff from the center to `radius`. The most predictable.
  falloff_smooth, ///< Smooth falloff, flat at the center, fully off at `radius`. Classic glow look.
  falloff_none,   ///< Even brightness within `radius`, then off sharply at the edge. Suits fixed light areas.
};

/// A 2D light source. Needs a transform on the same entity; `transform.pos` is the
/// light position, `transform.rot` is added to `angle`. Only has an effect once lighting
/// has been enabled with lighting_set().
///
/// Lighting is PBR (physically based rendering): each scene pixel has a base color
/// (albedo, which is the sprite image), a normal (normal map), metalness, roughness
/// and occlusion (material map), optionally emission, and each light is computed
/// with the Cook-Torrance model (GGX, Smith, Fresnel-Schlick, like pbr.fs in raylib's
/// examples) in linear HDR space, then tonemapped. See the 2D Lighting page in the docs.
struct light_2d {
  light_kind kind = light_point; ///< Light kind.
  /// Light color, sRGB like every other color in njin (the engine converts it to
  /// linear). The alpha channel is ignored.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f};
  /// Color temperature (kelvin). When greater than 0 the light color is additionally multiplied by
  /// the color of a glowing black body at that temperature: candle 1900, incandescent bulb 2700, daylight
  /// 6500, blue sky 10000. 0 turns it off.
  f32 temperature = 0.0f;
  /// Radiant intensity (linear space). A white rough surface right under the
  /// light, facing it, is lit by exactly this value; physical lights fall off quickly, so
  /// 4 to 12 is usually needed. It may exceed 1: lighting is HDR, the excess is tonemapped.
  f32 intensity = 6.0f;
  f32 radius = 200.0f; ///< Maximum radius (world units): beyond it the light does not reach.
  /// Size of the light source (world units). With `falloff_physical` this is the
  /// distance at which the brightness is halved. For every kind, it is the radius of the light
  /// source when casting shadows: the larger the source, the blurrier the shadow far from the occluder
  /// (penumbra). 0 gives a sharp shadow.
  f32 size = 32.0f;
  light_falloff falloff = falloff_physical; ///< Falloff curve. Ignored for directional lights.
  /// Direction a spot light shines, or the direction the light travels for a directional
  /// light, in degrees: 0 is right, 90 is down (clockwise). Added to
  /// `transform.rot`.
  f32 angle = 0.0f;
  f32 cone = 60.0f;      ///< Spot light: full opening angle, degrees.
  f32 softness = 0.3f;   ///< Spot light: softness of the cone edge, 0 (sharp) to 1 (fades from the center).
  /// Height of the light above the scene plane (world units). The lower the light,
  /// the more visible tilted surfaces are and the more specular reflection shifts. For a directional light use `elevation` instead.
  f32 height = 40.0f;
  f32 elevation = 45.0f; ///< Directional light: angle of the light above the scene plane, degrees (90 is straight down).
  bool cast_shadows = true; ///< Is blocked by occluders (light_occluder) and casts shadows.
  bool enabled = true;      ///< Turn off without removing the component.
};

/// Light occluder: a shape that blocks light and casts shadows. Needs a transform on the same
/// entity; the points are computed from `transform.pos`, rotated and scaled by the transform, so the occluder
/// moves, rotates and scales with the entity. Editing `points` every frame is fine too:
/// the shadow follows the new shape immediately.
///
/// There are three ways to get a shape:
/// - Give the points yourself: `points` is a closed polygon (`closed` by default), or an open
///   polyline (a thin wall, `closed = false`). The ready-made builders below
///   (light_occluder_box(), light_occluder_circle(), light_occluder_ellipse(),
///   light_occluder_capsule(), light_occluder_line()) cover common shapes.
/// - Follow the sprite shape: attach light_occluder_sprite() next to the sprite.
/// - Follow a tilemap: light_occluders_from_tiles().
///
/// A solid occluder does not shadow itself: a point inside the shape is not shadowed by that
/// shape, and a light inside the shape (a torch on a character) is not blocked by that
/// shape. A closed polygon may be wound in either direction. Each light considers at most the 64 nearest
/// edges in its area, so do not use too many edges for small objects.
struct light_occluder {
  std::vector<vec2> points; ///< The points in order. Closed back to the start when `closed`. At least 2.
  /// Closed polygon (solid, blocks from the outside in) or open line (thin, blocks both sides).
  bool closed = true;
  /// A closed polygon that is a hole in a solid mass: the inside is empty space, the solid
  /// is outside. Use it for the inner ring of a room with thick walls (the outer ring is
  /// solid, the inner ring is a hole). light_occluders_from_tiles() sets it for hole rings.
  bool hole = false;
  /// Farthest distance from the anchor point to a point of the shape, so the engine can quickly reject occluders
  /// outside the view without walking the points. The builders set it. Leave 0
  /// and the engine measures it every frame (a little slower with thousands of occluders); if
  /// set, it must be exact or larger, otherwise the occluder may be missed at the edge.
  f32 reach = 0.0f;
};

/// Rectangular occluder.
/// @param size Size (width, height).
/// @param origin Anchor point as a fraction of the size, like sprite::origin: `{0.5, 1}` is bottom-center, good for a tree trunk base.
/// @return Rectangular occluder.
inline light_occluder light_occluder_box(vec2 size, vec2 origin = {0.5f, 0.5f}) {
  const f32 x0 = -origin.x * size.x, y0 = -origin.y * size.y;
  const f32 reach = std::sqrt(std::max(x0 * x0, (x0 + size.x) * (x0 + size.x)) + std::max(y0 * y0, (y0 + size.y) * (y0 + size.y)));
  return light_occluder{{{x0, y0}, {x0 + size.x, y0}, {x0 + size.x, y0 + size.y}, {x0, y0 + size.y}}, true, false, reach};
}

/// Elliptical occluder (a regular polygon with enough edges). A circle when both radii are equal.
/// @param radii Radii along the two axes.
/// @param center Center, measured from the entity's anchor point.
/// @param segments Number of edges, from 3. More edges are rounder but cost more per light.
/// @return Occluder.
inline light_occluder light_occluder_ellipse(vec2 radii, vec2 center = {0.0f, 0.0f}, i32 segments = 16) {
  light_occluder o;
  const i32 n = segments < 3 ? 3 : segments;
  for (i32 i = 0; i < n; i++) {
    const f32 a = 6.28318530718f * (f32)i / (f32)n;
    o.points.push_back({center.x + std::cos(a) * radii.x, center.y + std::sin(a) * radii.y});
  }
  o.reach = std::hypot(center.x, center.y) + std::max(radii.x, radii.y);
  return o;
}

/// Circular occluder.
/// @param radius Radius.
/// @param center Center, measured from the entity's anchor point.
/// @param segments Number of edges, from 3.
/// @return Occluder.
inline light_occluder light_occluder_circle(f32 radius, vec2 center = {0.0f, 0.0f}, i32 segments = 16) {
  return light_occluder_ellipse({radius, radius}, center, segments);
}

/// Capsule-shaped occluder: the segment from `a` to `b` swollen by `radius`.
/// Good for a standing person, a tree trunk, a pillar: long with round ends.
/// @param a Center of the first end, measured from the entity's anchor point.
/// @param b Center of the second end.
/// @param radius Radius.
/// @param arc_segments Number of edges per half circle at the two ends.
/// @return Occluder.
inline light_occluder light_occluder_capsule(vec2 a, vec2 b, f32 radius, i32 arc_segments = 6) {
  light_occluder o;
  const f32 dx = b.x - a.x, dy = b.y - a.y;
  const f32 len = std::sqrt(dx * dx + dy * dy);
  const f32 base = len > 1e-6f ? std::atan2(dy, dx) : 0.0f;
  const i32 n = arc_segments < 1 ? 1 : arc_segments;
  // The half circle around `b`, from the left of the axis to the right, then the one around `a`.
  for (i32 i = 0; i <= n; i++) {
    const f32 t = base - 1.57079632679f + 3.14159265359f * (f32)i / (f32)n;
    o.points.push_back({b.x + std::cos(t) * radius, b.y + std::sin(t) * radius});
  }
  for (i32 i = 0; i <= n; i++) {
    const f32 t = base + 1.57079632679f + 3.14159265359f * (f32)i / (f32)n;
    o.points.push_back({a.x + std::cos(t) * radius, a.y + std::sin(t) * radius});
  }
  o.reach = std::max(std::hypot(a.x, a.y), std::hypot(b.x, b.y)) + radius;
  return o;
}

/// Occluder that is a thin polyline, blocking both sides: wall, fence, cliff edge.
/// @param points The points in order, at least 2.
/// @return Open occluder.
inline light_occluder light_occluder_line(std::vector<vec2> points) {
  light_occluder o{std::move(points), false};
  for (const vec2 &p : o.points)
    o.reach = std::max(o.reach, std::hypot(p.x, p.y));
  return o;
}

/// Occluder following the sprite shape: the outline of the sufficiently opaque pixels in the
/// currently shown frame of the sprite on the same entity, so the shadow has the exact shape of the tree or
/// monster, and changes with each animation frame, horizontal flip, rotation and scale. Needs a sprite
/// and a transform; use it instead of light_occluder on that entity.
///
/// The outline is computed once for each frame of each image and then cached, so the cost
/// is only the first time a frame appears (reading the image back from the graphics card). An image drawn into a render
/// texture cannot be used. An image packed in an atlas can.
struct light_occluder_sprite {
  /// Pixels with alpha at or above this threshold are solid, 0..1.
  f32 alpha = 0.5f;
  /// Maximum deviation (image pixels) of the outline from the real pixel outline. 0 keeps
  /// every stair step (many edges); 1 to 2 smooths it down to a few edges, much cheaper.
  f32 simplify = 1.0f;
};

/// **Per-pixel** occluder: the sufficiently opaque pixels of the sprite image (or of `mask`) block light and
/// cast shadows exactly per pixel, with no shape to build. Needs a sprite and a transform on the same entity.
///
/// The method follows mattdesl's "2D Pixel-Perfect Shadows": occluders are drawn into
/// an image (the occluder map), then for each light, ray-march along each angle around the light to find
/// at what distance the first occluder is (a 1D shadow map). The cost depends on the size of the area
/// the light reaches, **not on the number of occluders**, so it suits thousands of trees, grass blades, rocks.
///
/// Compared with light_occluder and light_occluder_sprite (built from polygons): the shadow follows every
/// pixel of the image, including small details and holes; but only occluders inside the screen image
/// extended by `lighting_desc::occluder_margin` cast shadows, and there is no thin "open line".
/// Both can be used at the same time. Shadows are soft according to `light_2d::size`, softening away from the occluder.
///
/// The occluder itself is still lit (a point inside the first occluder on the ray is not
/// shadowed by it), and a light inside an occluder is not blocked by that occluder.
struct light_occluder_pixels {
  /// Image used as the occluder: pixels with alpha at or above `lighting_desc::pixel_alpha` are
  /// solid. Same size and frame layout as `sprite::texture`. Empty means use the sprite's own
  /// image. Use it so that only the tree trunk blocks light, not the whole canopy.
  texture_handle mask{};
};

/// Outlines the solid tiles of a tilemap into occluders, so walls and cliffs block light.
///
/// Only edges between a solid tile and a non-solid tile become outline, and collinear
/// edges are merged, so a big wall block has only a few edges. The result is computed from the tilemap's
/// origin: attach each occluder to an entity that has the tilemap's transform. Recompute
/// when the tilemap changes (digging a wall, opening a door).
/// @param map Tilemap.
/// @param solid Tells whether a tile (index in the tileset, without flip bits) blocks light.
/// @param simplify Smoothing, in tiles; 0 keeps it as is.
/// @return Closed occluders (one ring for each connected block and each hole in a block).
std::vector<light_occluder> light_occluders_from_tiles(const tilemap &map, const std::function<bool(i32 tile)> &solid,
                                                       f32 simplify = 0.0f);

/// How HDR lighting is compressed to the screen. All run after exposure (lighting_desc::exposure).
enum light_tonemap : i32 {
  /// Keeps tones below 0.6 unchanged, only smoothly squeezing the brighter part down to 1. Pixel art colors at
  /// moderately bright spots are not changed, so it is the easiest to use. Default.
  tonemap_shoulder,
  tonemap_reinhard, ///< `x / (1 + x)`: simple, soft, but washes out and darkens the whole scene.
  tonemap_aces,     ///< ACES filmic (Narkowicz's approximation): cinematic contrast, moderate saturation. Set `exposure` higher.
};

/// Global lighting settings. Set with lighting_set().
struct lighting_desc {
  /// Enables lighting. When off the scene is drawn as normal, at no cost. When on, the whole
  /// world goes through the lighting passes and tonemap (before post_fx and the camera's post
  /// shader). Lighting only runs when there are lights, or an ambient that darkens the scene.
  bool enabled = false;
  /// Base light, present everywhere including where there is no light, multiplied by the base color and the
  /// surface's ambient occlusion. sRGB color like every color in njin.
  /// White does not darken at all, black is fully dark. Night is usually a dark blue.
  rgba ambient{0.12f, 0.14f, 0.24f, 1.0f};
  /// Multiplier applied to all the light before tonemap, ambient included. For adjusting the overall
  /// brightness (for example gradually reducing it at dawn), like a camera's exposure.
  f32 exposure = 1.0f;
  /// How HDR lighting is compressed to the screen.
  light_tonemap tonemap = tonemap_shoulder;
  /// Number of columns of the shadow map each light using light_occluder_pixels gets: the number of angles around a point light (the number of strips
  /// of a directional light). 0 picks automatically: enough for the rays to be about one pixel apart at the edge of the widest light, so
  /// thin occluders do not slip between two rays (with fewer, shadows far from the light break into fan-like spokes). Set a
  /// specific number (256 to 4096) to cap the cost.
  i32 shadow_columns = 0;
  /// Alpha threshold of light_occluder_pixels, 0..1: pixels at or above this threshold are solid.
  f32 pixel_alpha = 0.5f;
  /// light_occluder_pixels occluders are read from the screen image extended by this distance in every direction
  /// (world units): occluders farther than that cast no shadow. Larger costs more memory and fill.
  f32 occluder_margin = 96.0f;
  /// Size of the light image relative to the world image, 0.25 to 1. 1 is full
  /// detail (needed for pixel art). Smaller computes the whole lit image at a lower
  /// resolution and scales it up: much faster, but blurry, only suitable for weak machines.
  f32 scale = 1.0f;
  /// Shadow length of directional lights (world units): an occluder farther behind than this
  /// does not shadow the point being evaluated. An object of height H lit by the sun at angle `elevation`
  /// casts a shadow `H / tan(elevation)` long: for a tree of height 24 and a sun at 30 degrees that is
  /// about 40. The default 600 is a nearly infinite shadow, only suitable when occluders are sparse; in a dense
  /// forest it drowns the whole map in shadow. Applies to both polygon and per-pixel shadows.
  f32 shadow_reach = 600.0f;
};

/// Sets the lighting settings. May be changed every frame (for example ambient by time of day).
/// @param ctx Engine context.
/// @param desc New settings.
void lighting_set(njin_ctx &ctx, const lighting_desc &desc);

/// Lighting settings currently in use. Edit the copy, then call lighting_set() again.
/// @param ctx Engine context.
/// @return Current settings.
lighting_desc lighting_get(const njin_ctx &ctx);

/// Color of a glowing black body at a given temperature.
///
/// Use it to set `light_2d::color` or to tint the ambient (dawn 3500,
/// noon 6500, cold moon 9000). An approximation within 1000 to 40000 kelvin.
/// @param kelvin Color temperature.
/// @return Color (alpha is 1), normalized so the brightest channel equals 1.
rgba light_color_kelvin(f32 kelvin);
/// @}
} // namespace njin
