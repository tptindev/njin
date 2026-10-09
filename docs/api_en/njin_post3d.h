#pragma once
#include "_math.h"
#include "_types.h"

namespace njin {
struct context;

/// @addtogroup grp_post3d
/// @{

/// Screen effects of the 3D scene, worked out from the depth of the 3D pass:
/// ambient occlusion (SSAO), reflections (SSR), motion blur, light shafts and
/// lens flare.
///
/// Every effect is off at its default value: `post3d{}` draws exactly as if it
/// did not exist. Set with post3d_set(), changeable every frame. Only applies to
/// 3D passes into the world (begin_3d() without a render texture), not to passes
/// into a render texture. Order in end_3d(): after the opaque shapes come decals,
/// SSAO, SSR; then glass, water and 3D particles; then light shafts, lens flare and
/// motion blur. post_fx (njin_post.h) and the game's own shader run last, on the
/// whole image.
struct post3d {
  /// @name Ambient occlusion (SSAO)
  /// Wall corners, gaps between two objects and the feet of objects on the floor
  /// darken, as ambient light has a hard time getting in. Worked out from the
  /// depth, so every opaque shape gets it, the game's models included.
  /// @{
  f32 ssao = 0.0f;           ///< Strength, 0..1. 0 is off.
  f32 ssao_radius = 0.6f;    ///< Range looked at around each point, 3D units. The size of the gaps that darken.
  i32 ssao_samples = 12;     ///< Samples per pixel, 4..32. More is smoother and costs more.
  bool ssao_half = true;     ///< Worked out at half resolution, then scaled up following the depth: four times cheaper.
  /// @}

  /// @name Reflections (SSR)
  /// Surfaces with `material3d::reflect` above 0 (polished floors, metal, puddles)
  /// reflect what is on screen, found by marching rays over the depth image.
  /// What is off screen or hidden cannot be reflected: there the reflection fades
  /// to the fog colour (`light3d::fog_color`, the horizon colour with draw_sky3d()).
  /// @{
  f32 ssr = 0.0f;            ///< Strength, 0..1, multiplied by each surface's `reflect`. 0 is off.
  f32 ssr_distance = 20.0f;  ///< Longest ray, 3D units.
  i32 ssr_steps = 48;        ///< Steps marched per ray, 8..128.
  f32 ssr_thickness = 0.5f;  ///< How thick a surface counts as when a ray passes behind it.
  f32 ssr_sky = 1.0f;        ///< How much of the fog colour a ray that hits nothing reflects, 0..1.
  /// @}

  /// @name Motion blur
  /// The image smears along the way each pixel slides on screen between two frames:
  /// when the camera moves or turns, and when an object moves on its own (meshes and
  /// models drawn by the 3D calls, following the pose too when they have bones; see
  /// draw3d_motion_id()). A moving object's smear spreads over the background at its
  /// edge. A still camera and still objects do not blur.
  /// @{
  f32 motion_blur = 0.0f;       ///< Share of one frame's motion that is blurred, 0..1 (0.5 like a film camera). 0 is off.
  i32 motion_blur_samples = 8;  ///< Samples along the smear, 2..32.
  /// @}

  /// @name Light shafts
  /// Rays of light spreading from the sun through gaps between objects, when the
  /// sun is in or near the frame. The sun is the direction of `light3d::direction`
  /// (draw_sky3d() sets it from the time of day); the sky is where no 3D shape is.
  /// @{
  f32 shafts = 0.0f;            ///< Brightness, 0..2. 0 is off.
  f32 shafts_length = 0.8f;     ///< How much of the way from each point to the sun the rays reach, 0..1.
  rgba shafts_color{1.0f, 0.95f, 0.85f, 1.0f}; ///< Colour multiplied with the sunlight's.
  /// @}

  /// @name Lens flare
  /// A glow and spots of light along the line from the sun through the centre of
  /// the screen, like light bouncing inside a lens. Fades when objects hide the sun
  /// or it leaves the frame.
  /// @{
  f32 flare = 0.0f;             ///< Brightness, 0..2. 0 is off.
  f32 flare_halo = 0.5f;        ///< Brightness of the halo ring round the centre, multiplied by `flare`.
  /// @}

  /// @name Temporal anti-aliasing (TAA)
  /// Each frame the 3D projection is moved by a small fraction of a pixel (to a different place each frame), then
  /// the image is blended with the images of the frames before, moved back into place by the depth and the camera's
  /// motion: slanted edges lose their jaggies, thin edges stop flickering. Only the first 3D pass into the world of
  /// each frame is smoothed; 2D drawn after end_3d() is not touched. Meshes and models (following the pose too
  /// when they have bones) have a velocity of their own, so the edges of moving objects are smoothed as well (see
  /// draw3d_motion_id()). SDF shapes, draw_instanced3d() and the terrain, grass and water follow the camera's motion
  /// only: wherever the old image no longer matches (a very different depth, a colour outside the colours around
  /// it) the old image is dropped, so it leaves no trail, but their edges stay jagged while they move on their own.
  /// @{
  bool taa = false;           ///< Turns TAA on.
  f32 taa_sharpen = 0.25f;    ///< Sharpens the image back after blending, 0..1. 0 is no sharpening.
  /// @}
};

/// Sets the 3D screen effects, for every 3D pass from this frame on. `post3d{}`
/// turns them all off.
/// @param ctx The engine context.
/// @param fx The effects.
void post3d_set(context &ctx, const post3d &fx);

/// The effects that are set.
/// @param ctx The engine context.
/// @return The value set by post3d_set(), or the default.
post3d post3d_get(const context &ctx);

/// Names the next 3D draw, so TAA and motion blur know where it was last frame.
///
/// Usually not needed. The velocity of each mesh and model comes from where that
/// same draw was last frame: an entity with njin::model3d is recognised by its
/// entity; other draws by their model (or shape: box, sphere...) and their order in
/// the pass, so a scene drawn in the same order every frame matches itself. Call this
/// when the order changes between frames (a list of enemies thinned out, sorted by
/// distance): a number of its own for each object that does not change from frame to
/// frame, for example its id. Applies only to the very next draw in the open pass.
/// An object that jumps more than a quarter of the screen in one frame (a teleport)
/// is treated as having no velocity of its own.
/// @param ctx The engine context.
/// @param id A non-zero number, one per object.
void draw3d_motion_id(const context &ctx, u64 id);

/// How a decal covers the surface.
enum decal3d_blend {
  /// Multiplies the decal's colour into the surface: only darkens, keeping the
  /// surface's light and shadows (bullet holes, scorch marks, blood, footprints,
  /// mud).
  decal3d_multiply,
  /// Lays the decal's colour over like paint, lit by the sun and the ambient light
  /// (no shadows): paint, chalk, stickers, light-coloured signs on dark ground.
  decal3d_paint,
};

/// A decal: an image projected onto every opaque shape inside a box (walls,
/// floors, models, terrain), following the surface's shape.
///
/// The image lies on the box's xz face and projects along the box's y axis: by
/// default (no rotation) it projects down onto the floor. To put it on a wall,
/// turn the box so its y axis points out of the wall (for example
/// `rotation = {90, 0, 0}` for a wall facing +z). The more a surface slants from
/// the projection axis, the fainter the decal (see `angle_fade`), so marks do not
/// stretch along the sides.
struct decal3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Centre of the box.
  vec3 rotation{0.0f, 0.0f, 0.0f}; ///< Rotation, degrees, like njin::transform3d::rotation.
  /// Size of the box: `x` and `z` are the image's width and length, `y` how deep
  /// the decal reaches (it covers every surface in that range; thin keeps it off
  /// the neighbouring objects).
  vec3 size{1.0f, 0.5f, 1.0f};
  /// The image. Invalid gives a round, soft-edged spot of `color`. Images packed
  /// into an atlas are ignored (as with material3d::texture).
  texture_handle texture{};
  rect source{};                  ///< Area within the image, pixels. Size 0 is the whole image.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Colour multiplied into the image; `a` is the strength.
  decal3d_blend blend = decal3d_multiply; ///< How it covers.
  f32 lifetime = 0.0f;            ///< How long it lives, seconds (game time, stops while paused). 0 is for ever.
  f32 fade = 1.0f;                ///< Fades out over this many last seconds of `lifetime`.
  /// Surfaces whose normal turns away from the projection axis by more than this
  /// get no decal: the cosine of the angle, 0..1. 0.3 (the default) fades out from
  /// about 70 degrees.
  f32 angle_fade = 0.3f;
};

/// Rotation for `decal3d_desc::rotation` that lines the decal's projection axis
/// up with a surface's normal: the decal then lies flat on that surface. Use it
/// with the normal of a ray hit (ray3d_hit::normal, physics3d_raycast()) to put a
/// bullet hole right where the shot landed.
/// @code
/// const njin::ray3d_hit hit = njin::physics3d_raycast(ctx, shot, 100.0f);
/// if (hit.hit)
///   njin::decal3d_add(ctx, {.position = hit.point, .rotation = njin::decal3d_rotation(hit.normal),
///                           .size = {0.2f, 0.2f, 0.2f}, .texture = bullet_hole});
/// @endcode
/// @param normal The normal (need not be of length 1).
/// @return The rotation, degrees.
vec3 decal3d_rotation(vec3 normal);

/// Adds a decal. It shows in every 3D pass into the world until its `lifetime`
/// is over, it is removed, or a newer decal takes its place (see
/// decal3d_set_max()).
/// @param ctx The engine context.
/// @param desc The decal.
/// @return Its handle, or an invalid handle if the position, rotation or size is not a finite number.
decal3d_handle decal3d_add(context &ctx, const decal3d_desc &desc);

/// Removes a decal. An invalid handle or a decal that is already gone is ignored.
/// @param ctx The engine context.
/// @param handle The decal.
void decal3d_remove(context &ctx, decal3d_handle handle);

/// Removes every decal.
/// @param ctx The engine context.
void decal3d_clear(context &ctx);

/// How many decals there are.
/// @param ctx The engine context.
/// @return The number of decals.
i32 decal3d_count(const context &ctx);

/// The most decals there can be, 256 by default. Adding past it replaces the
/// oldest; lowering it below the current count removes the oldest.
/// @param ctx The engine context.
/// @param max The maximum, 1..4096.
void decal3d_set_max(context &ctx, i32 max);
/// @}
} // namespace njin
