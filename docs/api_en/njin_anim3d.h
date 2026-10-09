#pragma once
#include "_types.h"
#include "njin_3d.h"
#include <functional>

namespace njin {
struct context;

/// @addtogroup grp_anim3d
/// @{

/// A chain of spring bones: bone `bone` and every bone below it (hair, a tail,
/// whiskers, the flap of a coat). Each bone lags with inertia as the character
/// moves, swings, then returns to the animation's pose. The parameters are those
/// of VRM spring bones, so the numbers in a VRM file work as they are.
struct spring3d_chain {
  i32 bone = -1;          ///< The chain's root bone (model_bone_find()).
  /// Pull back to the animation's pose. Larger: stiffer, less swinging. 0.5 to 4 is usual.
  f32 stiffness = 1.0f;
  /// Damping, 0..1: 0 swings forever, 1 does not swing (only lags, then returns).
  f32 drag = 0.4f;
  f32 gravity = 0.0f;                     ///< Gravity pulling the bone tips, units per second.
  vec3 gravity_dir{0.0f, -1.0f, 0.0f};    ///< Direction of gravity in the world, length 1.
  f32 radius = 0.02f;                     ///< Collision radius of each bone's tip, in model units.
};

/// A collider the spring bones do not pass through (the head, the body, the
/// shoulders), attached to a bone so it follows the animation. A sphere when
/// `tail` equals `offset`, a capsule from `offset` to `tail` when they differ.
struct spring3d_collider {
  i32 bone = -1;          ///< The bone carrying the collider; -1 is the model's root.
  vec3 offset{};          ///< Centre, along the bone's three axes from its origin (model units).
  vec3 tail{};            ///< The capsule's other end, in the same axes as `offset`.
  f32 radius = 0.1f;      ///< Radius, in model units.
};

/// How to create a set of spring bones. The arrays only need to live until
/// spring3d_create() returns.
struct spring3d_desc {
  model_handle model{};                          ///< A model with bones.
  const spring3d_chain *chains = nullptr;        ///< The chains. A bone in two chains belongs to the first.
  u32 chain_count = 0;                           ///< Number of chains.
  const spring3d_collider *colliders = nullptr;  ///< The colliders, shared by every chain.
  u32 collider_count = 0;                        ///< Number of colliders.
};

/// Creates a set of spring bones for one character. Each character needs its own
/// set (it remembers where the bone tips are); several characters sharing a model
/// each get one.
///
/// @code
/// const njin::spring3d_chain hair{.bone = njin::model_bone_find(ctx, girl, "hair_1"), .stiffness = 1.5f};
/// const njin::spring3d_collider head{.bone = njin::model_bone_find(ctx, girl, "head"), .offset = {0, 0.1f, 0},
///                                    .tail = {0, 0.1f, 0}, .radius = 0.11f};
/// njin::spring3d_handle springs = njin::spring3d_create(
///     ctx, {.model = girl, .chains = &hair, .chain_count = 1, .colliders = &head, .collider_count = 1});
/// @endcode
/// @param ctx Engine context.
/// @param desc The model, the chains and the colliders.
/// @return A handle; invalid if the model has no bones or no chain has a valid
/// bone (a warning says why). Free it with spring3d_destroy().
spring3d_handle spring3d_create(context &ctx, const spring3d_desc &desc);

/// Runs the springs for `dt` seconds and writes the final pose of every bone into
/// `out`: pose `pose` (an animation, or bones the game set), with the spring bones
/// lagging and swinging. Simulated in the world, following the draw's
/// `transform`, so the hair flies back when the character runs. Pass `out` as
/// `model_pose::bones` to draw.
///
/// Call it once per frame for each character, before drawing. The springs run in
/// steps of 1/60 s (several steps when `dt` is large; a `dt` over 0.1 s is cut);
/// a `dt` of 0 (game paused) does not simulate, it only places the bones by the
/// current tips.
///
/// @code
/// njin::bone_pose3d bones[64];
/// const njin::model_pose walk{.anim = walk_anim, .time = t};
/// njin::spring3d_update(ctx, springs, walk, at, dt, bones, 64);
/// njin::draw_model_anim(ctx, girl, at, {.anim = walk_anim, .time = t, .bones = bones});
/// @endcode
/// @param ctx Engine context.
/// @param handle The springs.
/// @param pose The pose before the springs.
/// @param transform Position, rotation and scale of the draw (the scale should be the same on all three axes).
/// @param dt Time passed, seconds (usually delta()).
/// @param out Array receiving model_bone_count() bones, in model space as model_bone_pose().
/// @param count Number of elements in `out`.
/// @return Number of bones written; 0 if the handle is invalid or `count` is less than the bone count.
i32 spring3d_update(context &ctx, spring3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
                    bone_pose3d *out, i32 count);

/// Resets: on the next spring3d_update(), every spring bone starts still in the
/// animation's pose. Call it when teleporting the character (respawn, a portal),
/// so the hair does not fly along the whole way.
/// @param ctx Engine context.
/// @param handle The springs.
void spring3d_reset(context &ctx, spring3d_handle handle);

/// Destroys the springs. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle The springs.
void spring3d_destroy(context &ctx, spring3d_handle handle);

/// The standard name of a human (humanoid) bone from its name in the file:
/// understands the naming of Mixamo (`mixamorig:LeftForeArm`), Unreal
/// (`lowerarm_l`, `spine_02`), Unity and VRM (`LeftLowerArm`, `UpperChest`) and
/// Blender (`forearm.L`, `thigh.R`). The result is one of: `hips`, `spine`,
/// `chest`, `upper_chest`, `neck`, `head`, and per side (`left_`, `right_`):
/// `shoulder`, `upper_arm`, `lower_arm`, `hand`, `upper_leg`, `lower_leg`,
/// `foot`, `toes`, `thumb_1`..`thumb_3`, `index_1`..`index_3`, `middle_1`..,
/// `ring_1`.., `little_1`.. (finger joints counted from the hand).
/// @param name Bone name.
/// @return The standard name (a static string), or an empty string if not recognised.
const char *bone_humanoid_name(const char *name);

/// How to play the animation of model `source` on model `target`, whose skeleton
/// differs: other names, longer or shorter limbs.
struct retarget3d_desc {
  model_handle source{};  ///< The model with the animations (the source skeleton).
  model_handle target{};  ///< The model drawn (the target skeleton).
  /// Bone name pairs the game matches: `pairs[2 * i]` of the source goes with
  /// `pairs[2 * i + 1]` of the target. They come before the automatic matching.
  const char *const *pairs = nullptr;
  u32 pair_count = 0;     ///< Number of pairs.
  /// Match the remaining bones automatically: the same standard name
  /// bone_humanoid_name(), or the same name (without a prefix such as
  /// `mixamorig:`, case-insensitive).
  bool humanoid = true;
};

/// Prepares playing animation from `desc.source` on `desc.target`.
///
/// Each matched bone turns **relative to the rest pose**: the target bone turns by
/// exactly the angle the source bone turned away from its own rest pose, in model
/// space. So the two models need the same axes (the same up, the same facing) and
/// similar rest poses (both a T or both an A); otherwise the arms are off by
/// exactly the difference of the two rest poses. A target bone left unmatched
/// follows its parent as at rest. The hips are also moved with the source's
/// hips, times the ratio of the two models' hip heights (leg length), so a short
/// character takes shorter steps and the feet do not slide.
/// @param ctx Engine context.
/// @param desc The two models and how to match the bones.
/// @return A handle; invalid if a model has no bones or no bone could be matched
/// (a warning says why). Free it with retarget3d_destroy().
retarget3d_handle retarget3d_create(context &ctx, const retarget3d_desc &desc);

/// The target model's pose when the source model is in pose `pose`. Pass `out` as
/// `model_pose::bones` when drawing the target model.
///
/// @code
/// njin::bone_pose3d bones[80];
/// njin::retarget3d_pose(ctx, to_dwarf, {.anim = run, .time = t}, bones, 80);
/// njin::draw_model_anim(ctx, dwarf, at, {.bones = bones});
/// @endcode
/// @param ctx Engine context.
/// @param handle The retargeting.
/// @param pose The source model's pose (its animation, or bones the game set).
/// @param out Array receiving the target model's model_bone_count() bones, as model_bone_pose().
/// @param count Number of elements in `out`.
/// @return Number of bones written; 0 if the handle is invalid or `count` is less than the bone count.
i32 retarget3d_pose(const context &ctx, retarget3d_handle handle, const model_pose &pose, bone_pose3d *out,
                    i32 count);

/// The source bone matched with bone `bone` of the target model, to check the matching.
/// @param ctx Engine context.
/// @param handle The retargeting.
/// @param bone A bone of the target model, 0..model_bone_count() - 1.
/// @return A bone of the source model, or -1 if `bone` is unmatched or the handle is invalid.
i32 retarget3d_source_bone(const context &ctx, retarget3d_handle handle, i32 bone);

/// Destroys the retargeting. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle The retargeting.
void retarget3d_destroy(context &ctx, retarget3d_handle handle);

/// One leg for foot3d: thigh, shin, foot (model_bone_find()). The foot bone's
/// origin is the ankle.
struct foot3d_leg {
  i32 upper = -1; ///< Thigh.
  i32 lower = -1; ///< Shin.
  i32 foot = -1;  ///< Foot.
};

/// How to make a foot placer. The `legs` array only needs to live until
/// foot3d_create() returns.
struct foot3d_desc {
  model_handle model{}; ///< A model with bones, standing at its origin (soles at y = 0).
  /// The legs. nullptr: the two human legs are found by bone_humanoid_name()
  /// (`left_upper_leg`, `left_lower_leg`, `left_foot` and the right ones).
  const foot3d_leg *legs = nullptr;
  u32 leg_count = 0; ///< Number of legs in `legs`.
  /// How far a leg reaches up or down from the draw's origin, world units. More
  /// than the highest step the character climbs.
  f32 max_step = 0.5f;
  /// A surface steeper than this many degrees does not tilt the foot (walls, step edges).
  f32 max_tilt = 35.0f;
  /// How fast the feet follow new ground, per second. Large: at once; small:
  /// smoother. A foot going up is always twice as fast, so it does not sink into a
  /// step it just climbed.
  f32 smoothing = 15.0f;
  /// The way the knees bend, along the model's axes (usually where it faces). Used
  /// when the animation has the leg straight; a leg the animation bends keeps that side.
  vec3 knee_forward{0.0f, 0.0f, 1.0f};
};

/// Makes a foot placer for one character: each frame, each foot of the
/// animation is set on the ground right under it (stairs, slopes, rocks), the
/// hips lowered so the lower foot reaches, the knee bent by two-bone IK, the sole
/// tilted to the ground. A foot the animation lifts (a step) stays lifted, that
/// much above the ground under it. One per character (it remembers heights to
/// smooth them).
/// @param ctx Engine context.
/// @param desc The model and its legs.
/// @return A handle; invalid if the model has no bones or no leg has all three
/// bones (a warning says why). Free it with foot3d_destroy().
foot3d_handle foot3d_create(context &ctx, const foot3d_desc &desc);

/// The ground under a point, for foot3d_update(): takes where to start probing
/// (world) and how far down to probe; returns `true` and writes `point` (the hit)
/// and `normal` if there is ground. For terrain, for example:
/// `point = {p.x, terrain3d_height(ctx, t, p.x, p.z), p.z}`.
using foot3d_ground = std::function<bool(vec3 from, f32 distance, vec3 &point, vec3 &normal)>;

/// Sets the feet on the ground and writes the final pose to `out`, probing the
/// ground with physics3d_raycast() straight down under each ankle (it hits
/// static, dynamic and kinematic bodies and terrain height fields; not
/// character3d characters).
///
/// Order with the other steps: retarget3d_pose() first (if any), then
/// foot3d_update() with `pose.bones` set to that result, then spring3d_update()
/// with `pose.bones` set to foot3d's result, then draw. The draw's origin
/// (`transform`) is where the character stands (the bottom of a character3d's capsule).
///
/// @code
/// njin::bone_pose3d bones[64];
/// njin::foot3d_update(ctx, feet, {.anim = idle, .time = t}, at, dt, bones, 64);
/// njin::draw_model_anim(ctx, hero, at, {.bones = bones});
/// @endcode
/// @param ctx Engine context.
/// @param handle The foot placer.
/// @param pose The pose before placing the feet (an animation, or bones already worked out).
/// @param transform Position, rotation and scale of the draw (scale should be the same on all three axes).
/// @param dt Time passed, seconds, for smoothing. 0: placed at once, no smoothing.
/// @param out Array receiving model_bone_count() bones, as model_bone_pose().
/// @param count Number of elements in `out`.
/// @param weight How much to apply, 0..1: 0 is the animation as it is, 1 is the
/// feet fully on the ground (lower it while the character jumps).
/// @return Number of bones written; 0 if the handle is invalid or `count` is smaller than the number of bones.
i32 foot3d_update(context &ctx, foot3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
                  bone_pose3d *out, i32 count, f32 weight = 1.0f);

/// As above, but the ground is given by the game through `ground` (terrain, a
/// tile grid, any shape that is not in physics3d).
/// @param ctx Engine context.
/// @param handle The foot placer.
/// @param pose The pose before placing the feet.
/// @param transform Position, rotation and scale of the draw.
/// @param dt Time passed, seconds.
/// @param out Array receiving model_bone_count() bones.
/// @param count Number of elements in `out`.
/// @param ground The ground probe.
/// @param weight How much to apply, 0..1.
/// @return Number of bones written.
i32 foot3d_update(context &ctx, foot3d_handle handle, const model_pose &pose, const transform3d &transform, f32 dt,
                  bone_pose3d *out, i32 count, const foot3d_ground &ground, f32 weight = 1.0f);

/// How far the hips are moved (world units, negative is down) after the last
/// foot3d_update(), for a camera or a held item to follow.
/// @param ctx Engine context. @param handle The foot placer. @return The offset.
f32 foot3d_hip_offset(const context &ctx, foot3d_handle handle);

/// Resets: the next foot3d_update() places the feet at once, without smoothing
/// from where they were. Call it after teleporting the character.
/// @param ctx Engine context. @param handle The foot placer.
void foot3d_reset(context &ctx, foot3d_handle handle);

/// Destroys the foot placer. An invalid handle is ignored.
/// @param ctx Engine context. @param handle The foot placer.
void foot3d_destroy(context &ctx, foot3d_handle handle);
/// @}
} // namespace njin
