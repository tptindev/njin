#pragma once
#include "njin_3d.h"

namespace njin {
struct njin_ctx;

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
};

/// Creates a 3D physics body. The engine simulates every body in
/// `phase_fixed_update`, right after the game's systems in that phase.
/// @param ctx Engine context.
/// @param desc Body description.
/// @return Handle of the body, or invalid if the shape cannot be used.
body3d_handle body3d_create(njin_ctx &ctx, const body3d_desc &desc);

/// Destroys a body. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Body to destroy.
void body3d_destroy(njin_ctx &ctx, body3d_handle handle);

/// The body's current position and rotation, to draw it (`scale` is always 1).
/// @param ctx Engine context.
/// @param handle Body.
/// @return Position and rotation (degrees); the default if the handle is invalid.
transform3d body3d_transform(const njin_ctx &ctx, body3d_handle handle);

/// Moves a body instantly to a new position (a teleport, no collision along
/// the way), for example when resetting a level. Its velocity is kept.
/// @param ctx Engine context.
/// @param handle Body.
/// @param position New position.
/// @param rotation New rotation, degrees.
void body3d_set_position(njin_ctx &ctx, body3d_handle handle, vec3 position, vec3 rotation = {});

/// Makes a kinematic body reach `position` by the end of the next simulation
/// step. The engine sets the body's velocity so it gets there on time, so
/// characters and dynamic bodies standing on it are carried along. Call it
/// every fixed step with the new position (a platform going back and forth,
/// a lift).
/// @param ctx Engine context.
/// @param handle Kinematic body.
/// @param position Position to reach.
/// @param rotation Rotation to reach, degrees.
void body3d_move_kinematic(njin_ctx &ctx, body3d_handle handle, vec3 position, vec3 rotation = {});

/// The body's linear velocity, units per second.
/// @param ctx Engine context.
/// @param handle Body.
/// @return Velocity, or 0 if the handle is invalid.
vec3 body3d_velocity(const njin_ctx &ctx, body3d_handle handle);

/// Sets a dynamic body's linear velocity.
/// @param ctx Engine context.
/// @param handle Body.
/// @param velocity New velocity, units per second.
void body3d_set_velocity(njin_ctx &ctx, body3d_handle handle, vec3 velocity);

/// Gives a dynamic body a push (an impulse, kg * units per second) at its
/// centre: an explosion, a kick, a bullet hit.
/// @param ctx Engine context.
/// @param handle Body.
/// @param impulse Impulse.
void body3d_add_impulse(njin_ctx &ctx, body3d_handle handle, vec3 impulse);

/// The game's number attached to the body on creation (njin::body3d_desc::user).
/// @param ctx Engine context.
/// @param handle Body.
/// @return That number, or 0 if the handle is invalid.
u64 body3d_user(const njin_ctx &ctx, body3d_handle handle);

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
character3d_handle character3d_create(njin_ctx &ctx, const character3d_desc &desc);

/// Destroys a character. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Character.
void character3d_destroy(njin_ctx &ctx, character3d_handle handle);

/// Sets the velocity it should have for the next simulation step, units per
/// second.
/// @param ctx Engine context.
/// @param handle Character.
/// @param velocity Velocity.
void character3d_set_velocity(njin_ctx &ctx, character3d_handle handle, vec3 velocity);

/// The velocity after the last simulation step (cut down by walls and floors).
/// @param ctx Engine context.
/// @param handle Character.
/// @return Velocity.
vec3 character3d_velocity(const njin_ctx &ctx, character3d_handle handle);

/// Position of the character's feet.
/// @param ctx Engine context.
/// @param handle Character.
/// @return Position.
vec3 character3d_position(const njin_ctx &ctx, character3d_handle handle);

/// Moves the character instantly to a new feet position (a respawn, a
/// teleporter).
/// @param ctx Engine context.
/// @param handle Character.
/// @param position New feet position.
void character3d_set_position(njin_ctx &ctx, character3d_handle handle, vec3 position);

/// Whether the character is standing on something flat enough (after the
/// last simulation step).
/// @param ctx Engine context.
/// @param handle Character.
/// @return `true` if standing.
bool character3d_grounded(const njin_ctx &ctx, character3d_handle handle);

/// Velocity of what the character stands on: 0 on a static floor, the
/// platform's velocity on a kinematic body. Add it to the wanted velocity so
/// the character rides the platform.
/// @param ctx Engine context.
/// @param handle Character.
/// @return Velocity of the ground, or 0 if standing on nothing.
vec3 character3d_ground_velocity(const njin_ctx &ctx, character3d_handle handle);

/// The body the character stands on (to know which platform it reached).
/// @param ctx Engine context.
/// @param handle Character.
/// @return Body, or invalid if it stands on no body.
body3d_handle character3d_ground_body(const njin_ctx &ctx, character3d_handle handle);

/// Casts a ray against the bodies (characters are not hit): bullets, line of
/// sight, mouse picking.
/// @param ctx Engine context.
/// @param ray Ray (direction of length 1).
/// @param max_distance Farthest distance still counted, world units.
/// @param body If not nullptr, receives the body hit (invalid on a miss).
/// @return The nearest hit, if any.
ray3d_hit physics3d_raycast(const njin_ctx &ctx, const ray3d &ray, f32 max_distance,
                            body3d_handle *body = nullptr);

/// Sets gravity for dynamic bodies. Defaults to `{0, -9.81, 0}`. Characters
/// do not use this value: the game adds gravity to their velocity itself.
/// @param ctx Engine context.
/// @param gravity Acceleration, units per second squared.
void physics3d_set_gravity(njin_ctx &ctx, vec3 gravity);

/// The gravity used for dynamic bodies.
/// @param ctx Engine context.
/// @return Acceleration.
vec3 physics3d_gravity(const njin_ctx &ctx);
/// @}
} // namespace njin
