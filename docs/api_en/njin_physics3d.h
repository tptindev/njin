#pragma once
#include "njin_3d.h"

namespace njin {
struct context;

/// @addtogroup grp_physics3d
/// @{

/// How a 3D physics body moves.
enum body3d_motion {
  body3d_static,    ///< Never moves: floors, walls, fixed platforms. The cheapest.
  body3d_kinematic, ///< Moved by the game with body3d_move_kinematic(): moving platforms, doors. Not pushed by others, but carries and pushes them.
  body3d_dynamic,   ///< Driven by physics: falls, collides, rolls, gets pushed. Crates, balls, debris.
};

/// Describes a 3D physics body for body3d_create().
///
/// Shape and sizes follow the same convention as njin::shape3d, so a body and
/// the shape that draws it share their numbers: `shape`, `size` (box),
/// `radius`, `height` (capsule, cylinder, the whole shape along y). A torus
/// (njin::shape3d_torus) has no body.
///
/// With `model`, the body's shape comes from that model's triangle mesh, placed the
/// way draw_model() draws it with the same `position`, `rotation` and `scale`.
struct body3d_desc {
  shape3d_kind shape = shape3d_box;  ///< Shape: box, sphere, capsule or cylinder.
  vec3 position{0.0f, 0.0f, 0.0f};   ///< Centre.
  vec3 rotation{0.0f, 0.0f, 0.0f};   ///< Rotation, degrees, same order as njin::transform3d.
  vec3 size{1.0f, 1.0f, 1.0f};       ///< Box size along x, y, z.
  f32 radius = 0.5f;                 ///< Radius (sphere, capsule, cylinder).
  f32 height = 1.0f;                 ///< Overall height along y (capsule, cylinder).
  body3d_motion motion = body3d_static; ///< How it moves.
  f32 mass = 1.0f;                   ///< Mass, kg, dynamic bodies only.
  f32 friction = 0.5f;               ///< Friction, 0..1.
  f32 restitution = 0.0f;            ///< Bounciness, 0 (none) .. 1 (full bounce).
  /// The game's number attached to the body (an array index, an entity id...),
  /// read back with body3d_user() or from a physics3d_raycast() result.
  u64 user = 0;
  /// Take the shape from this model (model_load()) instead of `shape`. Static and
  /// kinematic bodies use the exact triangles: the floors, slopes and caves of a level
  /// made in Blender. Dynamic bodies use the convex hull of the vertices (the smallest
  /// convex shape around the model).
  model_handle model{};
  vec3 scale{1.0f, 1.0f, 1.0f}; ///< Scale of `model`, like njin::transform3d::scale.
  /// Detects only, does not collide: bodies and characters pass through, and the engine
  /// reports contact events (physics3d_contact()). For pickup zones, checkpoints, traps, goals.
  bool sensor = false;
};

/// Creates a 3D physics body. The engine simulates every body in
/// `phase_fixed_update`, right after the game's systems in that phase.
/// @param ctx Engine context.
/// @param desc Body description.
/// @return Handle of the body, or invalid if the shape cannot be used.
body3d_handle body3d_create(context &ctx, const body3d_desc &desc);

/// Destroys a body. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Body to destroy.
void body3d_destroy(context &ctx, body3d_handle handle);

/// The body's current position and rotation, to draw it (`scale` is always 1).
/// @param ctx Engine context.
/// @param handle Body.
/// @return Position and rotation (degrees); the default if the handle is invalid.
transform3d body3d_transform(const context &ctx, body3d_handle handle);

/// Moves a body instantly to a new position (a teleport, no collision along
/// the way), for example when resetting a level. Its velocity is kept.
/// @param ctx Engine context.
/// @param handle Body.
/// @param position New position.
/// @param rotation New rotation, degrees.
void body3d_set_position(context &ctx, body3d_handle handle, vec3 position, vec3 rotation = {});

/// Makes a kinematic body reach `position` by the end of the next simulation
/// step. The engine sets the body's velocity so it gets there on time, so
/// characters and dynamic bodies standing on it are carried along. Call it
/// every fixed step with the new position (a platform going back and forth,
/// a lift).
/// @param ctx Engine context.
/// @param handle Kinematic body.
/// @param position Position to reach.
/// @param rotation Rotation to reach, degrees.
void body3d_move_kinematic(context &ctx, body3d_handle handle, vec3 position, vec3 rotation = {});

/// The body's linear velocity, units per second.
/// @param ctx Engine context.
/// @param handle Body.
/// @return Velocity, or 0 if the handle is invalid.
vec3 body3d_velocity(const context &ctx, body3d_handle handle);

/// Sets a dynamic body's linear velocity.
/// @param ctx Engine context.
/// @param handle Body.
/// @param velocity New velocity, units per second.
void body3d_set_velocity(context &ctx, body3d_handle handle, vec3 velocity);

/// Gives a dynamic body a push (an impulse, kg * units per second) at its
/// centre: an explosion, a kick, a bullet hit.
/// @param ctx Engine context.
/// @param handle Body.
/// @param impulse Impulse.
void body3d_add_impulse(context &ctx, body3d_handle handle, vec3 impulse);

/// The game's number attached to the body on creation (njin::body3d_desc::user).
/// @param ctx Engine context.
/// @param handle Body.
/// @return That number, or 0 if the handle is invalid.
u64 body3d_user(const context &ctx, body3d_handle handle);

/// Describes a character for character3d_create(): an upright capsule that
/// walks on floors, steps up low steps, does not slide on gentle slopes, is
/// stopped by walls, and pushes dynamic bodies.
struct character3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Position of the **feet** (the bottom of the capsule).
  f32 radius = 0.3f;               ///< Capsule radius.
  f32 height = 1.8f;               ///< Overall capsule height.
  f32 max_slope = 50.0f;           ///< Steepest slope it can still stand on, degrees.
  f32 step_height = 0.3f;          ///< Highest step it walks up on its own.
  f32 mass = 70.0f;                ///< Mass, kg, when pushing dynamic bodies.
};

/// Creates a character. The game drives it by velocity: every fixed step,
/// call character3d_set_velocity() with the velocity it should have (the
/// game adds gravity and jumps itself), and the engine moves it right after,
/// sliding along walls and stopping on floors.
/// @param ctx Engine context.
/// @param desc Character description.
/// @return Handle of the character.
character3d_handle character3d_create(context &ctx, const character3d_desc &desc);

/// Destroys a character. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Character.
void character3d_destroy(context &ctx, character3d_handle handle);

/// Sets the velocity it should have for the next simulation step, units per
/// second.
/// @param ctx Engine context.
/// @param handle Character.
/// @param velocity Velocity.
void character3d_set_velocity(context &ctx, character3d_handle handle, vec3 velocity);

/// The velocity after the last simulation step (cut down by walls and floors).
/// @param ctx Engine context.
/// @param handle Character.
/// @return Velocity.
vec3 character3d_velocity(const context &ctx, character3d_handle handle);

/// Position of the character's feet.
/// @param ctx Engine context.
/// @param handle Character.
/// @return Position.
vec3 character3d_position(const context &ctx, character3d_handle handle);

/// Moves the character instantly to a new feet position (a respawn, a
/// teleporter).
/// @param ctx Engine context.
/// @param handle Character.
/// @param position New feet position.
void character3d_set_position(context &ctx, character3d_handle handle, vec3 position);

/// Whether the character is standing on something flat enough (after the
/// last simulation step).
/// @param ctx Engine context.
/// @param handle Character.
/// @return `true` if standing.
bool character3d_grounded(const context &ctx, character3d_handle handle);

/// Velocity of what the character stands on: 0 on a static floor, the
/// platform's velocity on a kinematic body. Add it to the wanted velocity so
/// the character rides the platform.
/// @param ctx Engine context.
/// @param handle Character.
/// @return Velocity of the ground, or 0 if standing on nothing.
vec3 character3d_ground_velocity(const context &ctx, character3d_handle handle);

/// The body the character stands on (to know which platform it reached).
/// @param ctx Engine context.
/// @param handle Character.
/// @return Body, or invalid if it stands on no body.
body3d_handle character3d_ground_body(const context &ctx, character3d_handle handle);

/// Casts a ray against the bodies (characters are not hit, sensors are passed
/// through): bullets, line of sight, mouse picking.
/// @param ctx Engine context.
/// @param ray Ray (direction of length 1).
/// @param max_distance Farthest distance still counted, world units.
/// @param body If not nullptr, receives the body hit (invalid on a miss).
/// @return The nearest hit, if any.
ray3d_hit physics3d_raycast(const context &ctx, const ray3d &ray, f32 max_distance,
                            body3d_handle *body = nullptr);

/// A contact event from the last simulation step: two bodies, or a body and a
/// character, started or stopped touching.
struct contact3d {
  body3d_handle a{};              ///< First body.
  body3d_handle b{};              ///< Second body; invalid when the other side is a character.
  character3d_handle character{}; ///< Character touching `a`, when `b` is invalid.
  bool began = true;              ///< `true`: started touching. `false`: stopped touching.
  bool sensor = false;            ///< One of the two is a sensor (njin::body3d_desc::sensor).
  vec3 point{0.0f, 0.0f, 0.0f};   ///< Contact point, when `began`.
  /// Contact normal, when `began`: from `a` towards `b` (or towards the character).
  vec3 normal{0.0f, 0.0f, 0.0f};
};

/// Number of contact events from the last simulation step.
///
/// A pair is reported once when it starts touching and once when it stops, even
/// if it touches at several points. The engine simulates right after the game's
/// systems in `phase_fixed_update`, so read the events in that phase: each step
/// the game sees exactly the events of the previous step, none missed, none
/// repeated. A destroyed body gets no stopped-touching event.
///
/// @code
/// for (int i = 0; i < njin::physics3d_contact_count(ctx); i++) {
///   const njin::contact3d c = njin::physics3d_contact(ctx, i);
///   if (c.began && c.a.id == goal.id && c.character.id == player.id)
///     win();
/// }
/// @endcode
/// @param ctx Engine context.
/// @return Event count.
i32 physics3d_contact_count(const context &ctx);

/// Contact event `index` of the last simulation step.
/// @param ctx Engine context.
/// @param index 0..physics3d_contact_count() - 1.
/// @return The event, or a default one if `index` is invalid.
contact3d physics3d_contact(const context &ctx, i32 index);

/// Kind of joint for njin::joint3d_desc.
enum joint3d_kind {
  joint3d_fixed,    ///< Welds two bodies: keeps their relative position and angle.
  joint3d_point,    ///< Ball joint: rotates freely around `anchor` (chains, ragdolls).
  joint3d_hinge,    ///< Hinge: rotates around `axis` through `anchor` (doors, seesaws, wheels).
  joint3d_slider,   ///< Slides along `axis`, no rotation (pistons, drawers, sliding doors).
  joint3d_distance, ///< Keeps the distance between `anchor` and `anchor_b` in `[min, max]` (ropes, rods).
};

/// Describes a joint for joint3d_create(). Points and axes are in world
/// coordinates, at the time the joint is created.
struct joint3d_desc {
  joint3d_kind kind = joint3d_hinge; ///< Kind of joint.
  body3d_handle a{};                 ///< First body.
  /// Second body. Invalid attaches `a` to a fixed point of the world.
  body3d_handle b{};
  vec3 anchor{0.0f, 0.0f, 0.0f};     ///< Attachment point.
  vec3 anchor_b{0.0f, 0.0f, 0.0f};   ///< Attachment point on the `b` side, njin::joint3d_distance only.
  vec3 axis{0.0f, 1.0f, 0.0f};       ///< Rotation axis (hinge) or sliding axis (slider).
  /// Limits: angle, degrees (hinge); travel, world units, 0 being the position at creation
  /// (slider); distance (njin::joint3d_distance, both 0 keeps the distance at creation).
  /// Hinges and sliders need `min <= 0 <= max` (hinge angles within -180..180): values
  /// outside are clamped. `min >= max` means no limit, except for njin::joint3d_distance.
  f32 min = 0.0f;
  f32 max = 0.0f; ///< See `min`.
  /// Motor of hinges and sliders: maximum force (hinge: torque, N·m) used to hold the
  /// speed set with joint3d_set_motor(). 0 is no motor.
  f32 motor_force = 0.0f;
};

/// Connects two bodies (or a body and the world) with a joint.
///
/// @code
/// // A plank hanging from a hinge at its top edge, swinging when the character jumps on it.
/// const auto plank = njin::body3d_create(ctx, {.position = {0, 4, 0}, .size = {3, 0.2f, 1},
///                                              .motion = njin::body3d_dynamic, .mass = 20});
/// njin::joint3d_create(ctx, {.kind = njin::joint3d_hinge, .a = plank, .anchor = {0, 6, 0}, .axis = {1, 0, 0}});
/// @endcode
/// @param ctx Engine context.
/// @param desc Joint description.
/// @return Handle of the joint, or invalid if `a` is invalid.
joint3d_handle joint3d_create(context &ctx, const joint3d_desc &desc);

/// Destroys a joint. Destroying a body also destroys all its joints. An invalid
/// handle is ignored.
/// @param ctx Engine context.
/// @param handle Joint.
void joint3d_destroy(context &ctx, joint3d_handle handle);

/// Sets the motor speed of a hinge (degrees per second) or slider (units per
/// second), within `joint3d_desc::motor_force`. 0 stops and holds still.
/// @param ctx Engine context.
/// @param handle Joint with a `motor_force` above 0.
/// @param speed Speed.
void joint3d_set_motor(context &ctx, joint3d_handle handle, f32 speed);

/// Current angle of a hinge (degrees) or travel of a slider (units), measured
/// from when the joint was created.
/// @param ctx Engine context.
/// @param handle Joint.
/// @return The value, 0 for other joint kinds or an invalid handle.
f32 joint3d_position(const context &ctx, joint3d_handle handle);

/// Component: the entity follows a physics body. The engine reads and writes the
/// entity's njin::transform3d around each simulation step:
/// - dynamic body: after the step, the body's position and rotation are written to the transform;
/// - kinematic body: before the step, the body is moved to the transform (like
///   body3d_move_kinematic()), so the game only has to move the transform;
/// - static body: nothing.
///
/// Removing the component or destroying the entity destroys the body too.
///
/// @code
/// const auto crate = reg.create();
/// reg.emplace<njin::transform3d>(crate);
/// reg.emplace<njin::shape3d_render>(crate, njin::shape3d_render{.shape = {.kind = njin::shape3d_box}});
/// reg.emplace<njin::body3d>(crate, njin::body3d_create(ctx, {.position = {0, 3, 0},
///                                                            .motion = njin::body3d_dynamic}));
/// @endcode
struct body3d {
  body3d_handle handle{}; ///< Body from body3d_create().
};

/// Component: the entity follows a physics character. After each simulation step,
/// the character's feet position is written to the entity's `transform3d::position`
/// (the rotation is set by the game). Removing the component or destroying the
/// entity destroys the character too.
struct character3d {
  character3d_handle handle{}; ///< Character from character3d_create().
};

/// Sets gravity for dynamic bodies. Defaults to `{0, -9.81, 0}`. Characters
/// do not use this value: the game adds gravity to their velocity itself.
/// @param ctx Engine context.
/// @param gravity Acceleration, units per second squared.
void physics3d_set_gravity(context &ctx, vec3 gravity);

/// The gravity used for dynamic bodies.
/// @param ctx Engine context.
/// @return Acceleration.
vec3 physics3d_gravity(const context &ctx);
/// @}
} // namespace njin
