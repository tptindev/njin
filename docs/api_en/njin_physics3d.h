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

/// Pushes a dynamic body with a force (N, kg * units per second²) at its centre
/// of mass, during the next physics step: a jet, wind, a magnet. Unlike
/// body3d_add_impulse(), a force lasts one step only, so call it every fixed
/// step while it pushes. Forces in one step add up.
/// @param ctx Engine context.
/// @param handle Body. Ignored if it is not dynamic.
/// @param force Force.
void body3d_add_force(context &ctx, body3d_handle handle, vec3 force);

/// Like body3d_add_force(context &, body3d_handle, vec3) but applies the force
/// at `point` (world): an off-centre force both pushes and turns the body. A
/// propeller pushing a frame at its hub, a jet at the edge of a plank.
///
/// @code
/// // Propeller: thrust along the axis, growing with the square of the hinge's spin.
/// const f32 w = njin::joint3d_speed(ctx, rotor_hinge) / 360.0f; // turns per second
/// njin::body3d_add_force(ctx, frame, axis * (k * w * std::fabs(w)), hub);
/// @endcode
/// @param ctx Engine context.
/// @param handle Body. Ignored if it is not dynamic.
/// @param force Force.
/// @param point Where the force acts, world.
void body3d_add_force(context &ctx, body3d_handle handle, vec3 force, vec3 point);

/// Turns a dynamic body with a torque (N·m) about its centre of mass, during the
/// next physics step. It turns by the right-hand rule about `torque`. Call it
/// every fixed step while it turns.
/// @param ctx Engine context.
/// @param handle Body. Ignored if it is not dynamic.
/// @param torque Torque.
void body3d_add_torque(context &ctx, body3d_handle handle, vec3 torque);

/// Angular velocity of the body: the vector's direction is the axis it spins
/// about, its length the degrees per second (right-hand rule).
/// @param ctx Engine context.
/// @param handle Body.
/// @return Angular velocity, degrees per second, or 0 if the handle is invalid.
vec3 body3d_angular_velocity(const context &ctx, body3d_handle handle);

/// For the next physics step, a dynamic body carries an extra weight of `mass`
/// kg at `point`: a person hanging on it or climbing it (a character standing
/// on a body is done by the engine itself), a crate that is not a body. The
/// body is as heavy as both together, with the weight's inertia there and its
/// pull turning the body about its centre, as for a character standing on it
/// (character3d_desc::mass): a board leant on a wall slips out as a person
/// climbs high on it, and a light board does not shake. Call it every fixed
/// step for as long as it carries the weight.
/// @param ctx Engine context.
/// @param handle Body. Ignored if it is not dynamic.
/// @param mass Mass, kg. Ignored unless positive.
/// @param point Where it rests, world; kept inside the body.
void body3d_carry(context &ctx, body3d_handle handle, f32 mass, vec3 point);

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
  /// Mass, kg: the weight on a dynamic body the character stands on (a seesaw
  /// tips, a board lying on the floor stays still, a board leant on a wall slips
  /// out), shared among every point it stands on, the whole weight however
  /// light the body.
  f32 mass = 70.0f;
  /// Two characters that both have `push` on do not block each other: after
  /// each step, a pair overlapping on the horizontal is moved apart by the
  /// minimum translation vector, shared by mass (the heavier shoves the
  /// lighter aside), so a crowd slips past itself instead of jamming. Walls,
  /// bodies and characters without it still block as usual.
  bool push = false;
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

/// Switches a character on or off. A character off is not moved, hits
/// nothing and other characters walk through it, but it keeps its position
/// and can be put elsewhere with character3d_set_position(). Each character
/// on costs one collision pass every physics step, so a big crowd switches on
/// only the people near the camera, and the game moves the far ones along
/// their paths itself (no one sees them collide).
///
/// Characters (on) block each other: they do not walk through one another
/// but slide round.
/// @param ctx Engine context.
/// @param handle Character.
/// @param active `true` for on (the default when created).
void character3d_set_active(context &ctx, character3d_handle handle, bool active);

/// Whether a character is on (character3d_set_active()).
/// @param ctx Engine context.
/// @param handle Character.
/// @return `true` if on; `false` if off or the handle is invalid.
bool character3d_active(const context &ctx, character3d_handle handle);

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

/// The soft body the character stands on (a mattress, a trampoline,
/// softbody3d_desc::walkable). character3d_ground_body() is invalid then, and
/// character3d_ground_velocity() is the velocity of the soft surface under its
/// feet: add it to a jump to bounce higher while the trampoline throws up.
/// @param ctx Engine context.
/// @param handle Character.
/// @return Soft body, or invalid if it does not stand on one.
softbody3d_handle character3d_ground_soft(const context &ctx, character3d_handle handle);

/// The minimum translation vector that moves the capsule `a`-`b` of radius
/// `radius` out of every body it overlaps (not counting characters, sensors and
/// `ignore`): moving the capsule by it just ends the overlap. For a character's
/// arms and torso to move aside from a wall instead of passing through it.
///
/// @code
/// // A forearm inside a wall: turn the elbow so the hand moves out along the push.
/// const njin::vec3 push = njin::physics3d_capsule_push(ctx, elbow, wrist, 0.045f);
/// @endcode
/// @param ctx Engine context.
/// @param a One end of the capsule's axis (a cap's centre), world space.
/// @param b The other end.
/// @param radius The radius.
/// @param ignore A body not counted (what the character holds). Invalid counts them all.
/// @return The push, `{0, 0, 0}` if it overlaps nothing.
vec3 physics3d_capsule_push(const context &ctx, vec3 a, vec3 b, f32 radius, body3d_handle ignore = {});

/// Like physics3d_capsule_push() for a box (a foot or a hand measured from the
/// skin with model_bone_bounds()).
/// @param ctx Engine context.
/// @param center The box's centre, world space.
/// @param rotation Rotation, degrees, in the same order as njin::transform3d.
/// @param size The box's size along x, y, z.
/// @param ignore A body not counted. Invalid counts them all.
/// @return The push, `{0, 0, 0}` if it overlaps nothing.
vec3 physics3d_box_push(const context &ctx, vec3 center, vec3 rotation, vec3 size, body3d_handle ignore = {});

/// Moves a box along `motion` and finds the first thing it meets (not counting
/// characters, sensors, `ignore` and what the box already overlaps at the start).
/// Like physics3d_raycast() but for a whole solid: dropping a foot to find where it
/// rests on uneven ground.
///
/// @code
/// // A foot dropped at most 1 m: where it meets something is where it stands.
/// const njin::ray3d_hit h = njin::physics3d_box_cast(ctx, foot, rot, size, {0, -1, 0});
/// @endcode
/// @param ctx Engine context.
/// @param center The box's centre at the start, world space.
/// @param rotation Rotation, degrees, in the same order as njin::transform3d.
/// @param size The box's size along x, y, z.
/// @param motion Direction and distance to move it.
/// @param ignore A body not counted. Invalid counts them all.
/// @param body If not nullptr, receives the body met.
/// @return `distance`: how far the box went before meeting it; `point`, `normal`:
/// where, and the normal of the surface met.
ray3d_hit physics3d_box_cast(const context &ctx, vec3 center, vec3 rotation, vec3 size, vec3 motion,
                             body3d_handle ignore = {}, body3d_handle *body = nullptr);

/// Where a ray or a moving shape hit a soft body, for the versions of
/// physics3d_raycast(), physics3d_box_cast() and physics3d_hull_cast() that take `soft`.
struct soft3d_hit {
  softbody3d_handle soft{}; ///< Soft body hit; invalid if a body was hit or nothing was.
  i32 face = -1;            ///< Triangle hit: triangle `face` of softbody3d_indices() (index `3 * face`).
  i32 vertex = -1;          ///< The vertex of that triangle nearest the hit: to pin, pull or push the spot hit.
};

/// As physics3d_box_cast(), but the box meets soft bodies too (curtains, cloth,
/// balls), not only bodies. When a soft body comes first `*body` is invalid and
/// `*soft` says which soft body and triangle; when a body comes first
/// `soft->soft` is invalid.
/// @param ctx Engine context.
/// @param center Centre of the box at the start, world space.
/// @param rotation Rotation, degrees, in the same order as njin::transform3d.
/// @param size Size of the box along x, y, z.
/// @param motion Direction and distance to move.
/// @param ignore Body not counted. Invalid counts them all.
/// @param body If not nullptr, receives the body hit.
/// @param soft Receives the soft body hit; nullptr skips soft bodies, like the version without it.
/// @return As physics3d_box_cast().
ray3d_hit physics3d_box_cast(const context &ctx, vec3 center, vec3 rotation, vec3 size, vec3 motion,
                             body3d_handle ignore, body3d_handle *body, soft3d_hit *soft);

/// Builds a convex hull from points (the smallest convex solid around them), to
/// query collisions with the true shape of a part: a foot, a hand from
/// model_bone_points(). The hull is not a body: it collides with nothing, it is only
/// for physics3d_hull_push() and physics3d_hull_cast(). Build it once, use it every
/// frame at a different place and turn.
///
/// @code
/// std::vector<njin::vec3> pts(njin::model_bone_points(ctx, man, foot_l, false, nullptr, 0));
/// njin::model_bone_points(ctx, man, foot_l, false, pts.data(), (njin::i32)pts.size());
/// const njin::hull3d_handle sole = njin::physics3d_hull_create(ctx, pts.data(), (njin::i32)pts.size());
/// @endcode
/// @param ctx Engine context.
/// @param points The points, in the hull's own axes.
/// @param count Number of points (at least 4, not all on one plane).
/// @return Handle, or invalid if the points make no solid.
hull3d_handle physics3d_hull_create(context &ctx, const vec3 *points, i32 count);

/// Destroys the hull. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param hull The hull.
void physics3d_hull_destroy(context &ctx, hull3d_handle hull);

/// Like physics3d_box_push() for a convex hull placed at `position`, turned by `rotation`.
/// @param ctx Engine context.
/// @param hull The hull.
/// @param position The origin of the hull's own axes, world space.
/// @param rotation Rotation, degrees, in the same order as njin::transform3d.
/// @param ignore A body not counted. Invalid counts them all.
/// @return The push, `{0, 0, 0}` if it overlaps nothing or the handle is invalid.
vec3 physics3d_hull_push(const context &ctx, hull3d_handle hull, vec3 position, vec3 rotation, body3d_handle ignore = {});

/// Like physics3d_box_cast() for a convex hull.
/// @param ctx Engine context.
/// @param hull The hull.
/// @param position The origin of the hull's own axes at the start, world space.
/// @param rotation Rotation, degrees, in the same order as njin::transform3d.
/// @param motion Direction and distance to move it.
/// @param ignore A body not counted. Invalid counts them all.
/// @param body If not nullptr, receives the body met.
/// @return As physics3d_box_cast(); no hit if the handle is invalid.
ray3d_hit physics3d_hull_cast(const context &ctx, hull3d_handle hull, vec3 position, vec3 rotation, vec3 motion,
                              body3d_handle ignore = {}, body3d_handle *body = nullptr);

/// As physics3d_hull_cast(), meeting soft bodies too, like the version of
/// physics3d_box_cast() with `soft`.
/// @param ctx Engine context.
/// @param hull Hull.
/// @param position Origin of the hull's own axes at the start, world space.
/// @param rotation Rotation, degrees, in the same order as njin::transform3d.
/// @param motion Direction and distance to move.
/// @param ignore Body not counted. Invalid counts them all.
/// @param body If not nullptr, receives the body hit.
/// @param soft Receives the soft body hit; nullptr skips soft bodies.
/// @return As physics3d_box_cast(); nothing hit if the handle is invalid.
ray3d_hit physics3d_hull_cast(const context &ctx, hull3d_handle hull, vec3 position, vec3 rotation, vec3 motion,
                              body3d_handle ignore, body3d_handle *body, soft3d_hit *soft);

/// The hull's edges, as pairs of points in its own axes, for debug drawing.
/// @param ctx Engine context.
/// @param hull The hull.
/// @param out Array receiving the points (edge i is `out[2i]`, `out[2i + 1]`), or nullptr to only count.
/// @param count Number of elements in `out`.
/// @return How many points there are (twice the number of edges).
i32 physics3d_hull_lines(const context &ctx, hull3d_handle hull, vec3 *out, i32 count);

/// Casts a ray against the bodies (characters are not hit, sensors are passed
/// through): bullets, line of sight, mouse picking.
/// @param ctx Engine context.
/// @param ray Ray (direction of length 1).
/// @param max_distance Farthest distance still counted, world units.
/// @param body If not nullptr, receives the body hit (invalid on a miss).
/// @return The nearest hit, if any.
ray3d_hit physics3d_raycast(const context &ctx, const ray3d &ray, f32 max_distance,
                            body3d_handle *body = nullptr);

/// As physics3d_raycast(), but the ray hits soft bodies too: a shot at a curtain
/// hits the curtain instead of passing through. When a soft body comes first
/// `*body` is invalid and `*soft` gives the soft body, the triangle and the
/// nearest vertex (to push the spot with softbody3d_add_impulse() or pin it);
/// when a body comes first `soft->soft` is invalid.
///
/// @code
/// njin::body3d_handle body;
/// njin::soft3d_hit soft;
/// const njin::ray3d_hit hit = njin::physics3d_raycast(ctx, shot, 100.0f, &body, &soft);
/// if (hit.hit && soft.soft.id != 0)
///   njin::softbody3d_add_impulse(ctx, soft.soft, shot.direction * 2.0f);
/// @endcode
/// @param ctx Engine context.
/// @param ray Ray (direction of length 1).
/// @param max_distance Farthest distance still counted, world units.
/// @param body If not nullptr, receives the body hit (invalid on a miss or a soft body hit).
/// @param soft Receives the soft body hit; nullptr skips soft bodies, like the version without it.
/// @return The nearest hit, if any.
ray3d_hit physics3d_raycast(const context &ctx, const ray3d &ray, f32 max_distance, body3d_handle *body,
                            soft3d_hit *soft);

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
  /// Gear: makes two hinges `joint_a`, `joint_b` turn at the ratio `ratio` (meshing
  /// gears, chains, belts). The wheels are body `a` of each hinge.
  joint3d_gear,
  /// Rack: makes hinge `joint_a` (the pinion) and slider `joint_b` (the rack) move
  /// together: the rack slides `ratio` units per turn of the pinion.
  joint3d_rack,
  /// Pulley: a rope from `anchor` on `a` over the fixed point `pulley_a`, across to
  /// `pulley_b` and down to `anchor_b` on `b`. The rope does not stretch: pull one
  /// side up and the other comes down.
  joint3d_pulley,
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
  /// njin::joint3d_pulley: total rope length, the `a` side plus `ratio` times the `b`
  /// side (both 0 is from 0 to the length at creation: the rope can go slack but not
  /// stretch).
  f32 min = 0.0f;
  f32 max = 0.0f; ///< See `min`.
  /// Motor of hinges and sliders: maximum force (hinge: torque, N·m) used to hold the
  /// speed set with joint3d_set_motor(). 0 is no motor.
  f32 motor_force = 0.0f;
  /// Spring of hinges, sliders and njin::joint3d_distance: oscillation frequency, Hz
  /// (stiffness that does not depend on mass: 1..5 soft, 10..20 stiff). The limits
  /// `[min, max]` become a spring: going past them pulls back. Without limits the
  /// spring holds the joint at its position at creation (angle, travel, distance).
  /// 0 is hard limits.
  f32 spring = 0.0f;
  /// Damping of the spring, 0 (bounces forever) .. 1 (returns without bouncing).
  f32 damping = 0.0f;
  /// First hinge of njin::joint3d_gear and njin::joint3d_rack (its body `a` is the
  /// gear). These two joints do not use `a` and `b`.
  joint3d_handle joint_a{};
  /// Second hinge (njin::joint3d_gear) or the rack's slider (njin::joint3d_rack);
  /// its body `a` is the wheel or the rack.
  joint3d_handle joint_b{};
  /// Ratio. njin::joint3d_gear: turns of gear `joint_a` while gear `joint_b` turns
  /// once (teeth of b / teeth of a); positive turns the other way (meshing gears),
  /// negative the same way (chain, belt), when both axes point the same way.
  /// njin::joint3d_rack: rack travel per turn of the pinion, units (2π × pinion
  /// radius); negative slides the other way. njin::joint3d_pulley: the `b` side
  /// counts `ratio` times (block and tackle: 2 pulls with half the force over twice
  /// the length).
  f32 ratio = 1.0f;
  vec3 pulley_a{0.0f, 0.0f, 0.0f}; ///< Fixed point the rope on the `a` side runs over (njin::joint3d_pulley).
  vec3 pulley_b{0.0f, 0.0f, 0.0f}; ///< Fixed point the rope on the `b` side runs over (njin::joint3d_pulley).
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

/// Changes the maximum motor force (`joint3d_desc::motor_force`) while running:
/// a throttle, a rubber-band motor weakening as the band unwinds. Above 0 turns
/// the motor on (it holds the speed of joint3d_set_motor(), by default 0, holding
/// still), 0 turns it off: the joint turns or slides freely.
///
/// @code
/// // Rubber-band motor: wind the axle `turns` turns the positive way; let go and the
/// // band turns it back with a torque that drops with the turns left.
/// turns += njin::joint3d_speed(ctx, axle) / 360.0f * dt;
/// njin::joint3d_set_motor(ctx, axle, turns > 0 ? -3600.0f : 0.0f);
/// njin::joint3d_set_motor_force(ctx, axle, std::max(turns, 0.0f) * 2.0f); // 2 N·m per turn
/// @endcode
/// @param ctx Engine context.
/// @param handle Hinge or slider; other kinds are ignored.
/// @param force Maximum force (hinge: torque, N·m), not negative.
void joint3d_set_motor_force(context &ctx, joint3d_handle handle, f32 force);

/// Current speed of a hinge (degrees per second, the same way as
/// joint3d_position()) or of a slider (units per second): counting turns, the
/// thrust of a propeller.
/// @param ctx Engine context.
/// @param handle Joint.
/// @return Speed, 0 for other joint kinds or an invalid handle.
f32 joint3d_speed(const context &ctx, joint3d_handle handle);

/// Force the joint is bearing to hold the two bodies to its rule (holding the
/// attachment point, limits, motor, rope tension), in the last physics step, N.
/// Compare it with a threshold to break the machine: too much and
/// joint3d_destroy() (a tooth snaps, a chain slips off, a weld breaks).
/// @param ctx Engine context.
/// @param handle Joint.
/// @return Force, 0 for joints that only hold an angle (njin::joint3d_gear) or an invalid handle.
f32 joint3d_force(const context &ctx, joint3d_handle handle);

/// Torque the joint is bearing to hold the angle between the two bodies (a hinge
/// holding its axis, angle limits, motor, a gear passing torque on), in the last
/// physics step, N·m. Taken about the point the joint holds: a hinge's `anchor`,
/// but the centre of body `a` for njin::joint3d_fixed. So a plank welded to a wall
/// and weighed down reports only a force (joint3d_force()), no torque at its foot;
/// to measure the bending torque at the foot, attach it with a hinge with narrow
/// limits (`min = -0.5`, `max = 0.5`) whose `anchor` is there.
/// @param ctx Engine context.
/// @param handle Joint.
/// @return Torque, 0 for joints that hold no angle (ball, distance, pulley) or an invalid handle.
f32 joint3d_torque(const context &ctx, joint3d_handle handle);

/// One part of a ragdoll: a bone of the model as a physics capsule along the
/// bone's y axis (the bone's direction with Blender rigs). By default the capsule
/// wraps that part's skin in the rest pose: the vertices pulled hardest by this
/// bone, or by a bone below it with no part of its own (fingers, collarbones).
/// Sizes are world units. Angle limits count from the file's rest pose (T or A
/// pose), not from the pose the ragdoll starts in.
struct ragdoll3d_bone {
  const char *name = nullptr; ///< Bone name (model_bone_find()).
  /// Capsule radius. 0 measures it from the skin (wrapping 80% of the vertices);
  /// 0.06 for a model without skin.
  f32 radius = 0.0f;
  /// Length from the bone's origin. 0 measures it from the skin (the capsule's
  /// length and offset follow the skin); without skin it reaches the child bone
  /// furthest along the bone, and a bone with no child ahead becomes a sphere.
  f32 length = 0.0f;
  /// Ball joint to the parent part: the largest swing of the bone's axis, degrees.
  f32 swing = 30.0f;
  f32 twist = 15.0f; ///< The largest twist around the bone's axis, degrees, both ways.
  /// A hinge instead of a ball joint (knees, elbows): rotates around the bone's
  /// x axis, the angle in `[bend_min, bend_max]`, degrees, within -180..180 and
  /// containing 0. A positive angle turns the bone's y axis towards its z axis
  /// (on the Quaternius mannequin, knees and elbows bend the positive way).
  /// `bend_min >= bend_max` is a ball joint.
  f32 bend_min = 0.0f;
  f32 bend_max = 0.0f; ///< See `bend_min`.
};

/// Description of a ragdoll for ragdoll3d_create().
struct ragdoll3d_desc {
  model_handle model{};     ///< A model with bones (model_load()).
  /// Where the model is being drawn, as draw_model_anim(). The scale must be the same on all three axes.
  transform3d transform{};
  model_pose pose{};        ///< The starting pose, usually the pose just drawn.
  /// The parts, each a different bone. Each part joins the part whose bone is
  /// nearest above it in the skeleton; exactly one part has no part above it
  /// (the root, usually the pelvis).
  const ragdoll3d_bone *bones = nullptr;
  u32 bone_count = 0;       ///< Number of parts in `bones`.
  f32 mass = 70.0f;         ///< Mass of the whole body, kg, shared among the parts by volume.
  f32 friction = 0.6f;      ///< Friction, 0..1.
  vec3 velocity{0.0f, 0.0f, 0.0f}; ///< Starting velocity of every part.
  u64 user = 0;             ///< body3d_user() of every part.
};

/// Turns a model with bones into a ragdoll: each part is a dynamic body, joined
/// to its parent part by a joint with angle limits, falling and colliding like
/// any body. Parts of the same ragdoll never collide with each other, only
/// with the world. Bones without a part (fingers,
/// toes, the root) follow the nearest part above them.
///
/// @code
/// // The Quaternius mannequin falls from the pose being drawn.
/// const njin::ragdoll3d_bone parts[] = {
///   {.name = "pelvis"},
///   {.name = "spine_02", .swing = 20, .twist = 15},
///   {.name = "Head", .swing = 40, .twist = 40},
///   {.name = "upperarm_l", .swing = 70, .twist = 30},
///   {.name = "lowerarm_l", .bend_min = 0, .bend_max = 140},
///   {.name = "thigh_l", .swing = 50, .twist = 15},
///   {.name = "calf_l", .bend_min = 0, .bend_max = 140},
///   // ... the right side like the left
/// };
/// rag = njin::ragdoll3d_create(ctx, {.model = man, .transform = at, .pose = pose,
///                                    .bones = parts, .bone_count = std::size(parts)});
/// @endcode
/// @param ctx Engine context.
/// @param desc Ragdoll description.
/// @return Handle, or invalid (with a warning in the log) if the model has no
/// bones, a bone name is not found or listed twice, or the parts do not join
/// into one tree.
ragdoll3d_handle ragdoll3d_create(context &ctx, const ragdoll3d_desc &desc);

/// Destroys the ragdoll and its bodies. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Ragdoll.
void ragdoll3d_destroy(context &ctx, ragdoll3d_handle handle);

/// The body of one part, to push it (body3d_add_impulse()), read its position,
/// or tell which part physics3d_raycast() and physics3d_contact() report. Do not
/// destroy it with body3d_destroy(): destroy the whole ragdoll.
/// @param ctx Engine context.
/// @param handle Ragdoll.
/// @param part Index in `ragdoll3d_desc::bones`.
/// @return The body, or invalid if the handle or `part` is invalid.
body3d_handle ragdoll3d_body(const context &ctx, ragdoll3d_handle handle, i32 part);

/// The collision shape of one part, in the world: the capsule (or sphere) the
/// engine measured from the skin, placed by that part's body now. For debug
/// drawing, with gizmos or draw_shape3d() for example.
/// @param ctx Engine context.
/// @param handle Ragdoll.
/// @param part Index in `ragdoll3d_desc::bones`.
/// @return The shape (njin::shape3d_capsule or njin::shape3d_sphere), or
/// `radius` 0 if the handle or `part` is invalid.
shape3d ragdoll3d_shape(const context &ctx, ragdoll3d_handle handle, i32 part);

/// The ragdoll's current pose, to draw the model with `model_pose::bones` at
/// `transform` (usually `ragdoll3d_desc::transform`).
///
/// @code
/// static njin::bone_pose3d bones[128];
/// njin::ragdoll3d_bones(ctx, rag, at, bones, 128);
/// njin::draw_model_anim(ctx, man, at, {.bones = bones});
/// @endcode
/// @param ctx Engine context.
/// @param handle Ragdoll.
/// @param transform Where the model will be drawn.
/// @param out Array receiving the pose, model_bone_count() elements.
/// @param count Number of elements in `out`.
/// @return Number of bones written (model_bone_count()), 0 if the handle is
/// invalid or `count` is less than the number of bones.
i32 ragdoll3d_bones(const context &ctx, ragdoll3d_handle handle, const transform3d &transform, bone_pose3d *out,
                    i32 count);

/// Ready-made shapes of a soft body (njin::softbody3d_desc::kind).
enum softbody3d_kind {
  /// A solid box `size`: an even lattice of points inside the box, keeping its
  /// volume. Mattress, jelly block, rubber block.
  softbody3d_box,
  /// A hollow sphere of radius `radius`: add `pressure` to make a ball.
  softbody3d_sphere,
  /// The surface of a triangle mesh (`mesh`, or every mesh of `model`), hollow
  /// like the sphere: a closed mesh with `pressure` stays inflated. Vertices at
  /// the same place (the file's UV seams) are merged into one.
  softbody3d_mesh,
};

/// Describes a soft body for softbody3d_create(). A soft body is a cloud of
/// points joined by springs that deforms each physics step under gravity,
/// collisions and the pressure inside it.
///
/// A soft body's vertices are numbered in the order the engine builds them: for
/// `softbody3d_mesh` the order of the mesh's vertices (a mesh with no two
/// vertices at the same place keeps its indices), for the box and the sphere
/// find them with softbody3d_nearest().
struct softbody3d_desc {
  softbody3d_kind kind = softbody3d_sphere; ///< Shape.
  vec3 position{0.0f, 0.0f, 0.0f};  ///< Centre.
  vec3 rotation{0.0f, 0.0f, 0.0f};  ///< Rotation, degrees, in the same order as njin::transform3d.
  vec3 size{1.0f, 1.0f, 1.0f};      ///< Size of the box along x, y, z.
  f32 radius = 0.5f;                ///< Radius of the sphere.
  /// Detail. Box: points along its longest edge (2..16), the other edges at the
  /// same spacing. Sphere: how many times the icosahedron's faces are split
  /// (1..4: 42, 162, 642, 2562 vertices). 0 is the default: 5 points for the
  /// box, 2 splits for the sphere.
  i32 detail = 0;
  /// The mesh for `softbody3d_mesh`, in the body's own axes (placed at
  /// `position`, turned by `rotation`). Triangles counter-clockwise seen from
  /// outside.
  mesh3d_data mesh{};
  /// Takes the mesh from this model (model_load()) for `softbody3d_mesh` instead
  /// of `mesh`, placed as draw_model() draws it with the same `position`,
  /// `rotation` and `scale`.
  model_handle model{};
  vec3 scale{1.0f, 1.0f, 1.0f};     ///< Scale of `model`.
  f32 mass = 1.0f;                  ///< Mass of the whole body, kg, shared evenly among the vertices.
  /// Stiffness of the edges, 0 (stretchy as soft rubber) .. 1 (does not stretch).
  f32 stiffness = 0.9f;
  /// Stiffness against folding, 0 (folds freely) .. 1 (keeps the curve it was
  /// made with). Not used by the box.
  f32 bend = 0.5f;
  /// The pressure inside while the body has the shape it was made with, Pa
  /// (N/m²). Squeeze it and the pressure rises, like a balloon. 0 is none (a
  /// hollow body slowly sags). A 1 kg ball of radius 0.5: about 50 (soft, sinks
  /// in when it lands) to 300 (taut); more than that and the edges stretch and
  /// the body swells beyond its starting size. Needs a closed surface.
  f32 pressure = 0.0f;
  f32 friction = 0.5f;              ///< Friction, 0..1.
  f32 restitution = 0.0f;           ///< Bounciness, 0..1.
  f32 damping = 0.1f;               ///< Damping: the velocity shrinks by this fraction each second.
  /// How much the air drags on the body's surface (wind, softbody3d_set_wind()).
  /// 0 is no air at all.
  f32 drag = 1.0f;
  /// Pinned vertices: they stay put, or follow softbody3d_move_pinned(). Pin and
  /// unpin at run time with softbody3d_pin().
  const u32 *pinned = nullptr;
  u32 pinned_count = 0;             ///< Number of elements in `pinned`.
  u64 user = 0;                     ///< The game's number, read back with softbody3d_user().
  /// Collides with the other soft bodies that have this on too: two cloths
  /// lying on each other, a ball dropped on a mattress. Every vertex keeps the
  /// thickness of both off the other's faces. `false` passes through other soft
  /// bodies (cheaper, when they never meet).
  bool collide_soft = true;
  /// Characters can stand on its upper side (a mattress, a trampoline, a block
  /// of jelly), sinking in under the character's weight
  /// (njin::character3d_desc::mass). On an upright face (a curtain) they still
  /// walk through, pushing the vertices aside. `false`: characters never stand on it.
  bool walkable = true;
};

/// Creates a soft body. Soft bodies collide with every body (fall on the floor,
/// drape over a crate) and with each other (njin::softbody3d_desc::collide_soft).
/// Characters (character3d_create()) stand on its upper side, and walk through
/// an upright face pushing its vertices aside, so a curtain does not block the
/// way. physics3d_raycast() and the `physics3d_*_push`, `_cast` queries pass
/// through soft bodies; the versions that take a njin::soft3d_hit hit them.
///
/// @code
/// // A bouncy ball: a sphere with pressure, dropped from 3 m.
/// const njin::softbody3d_handle ball = njin::softbody3d_create(
///     ctx, {.kind = njin::softbody3d_sphere, .position = {0, 3, 0}, .radius = 0.5f, .pressure = 200});
/// // Every frame, between begin_3d() and end_3d():
/// njin::draw_model(ctx, njin::softbody3d_model(ctx, ball), {});
/// @endcode
/// @param ctx Engine context.
/// @param desc Description of the soft body.
/// @return Handle, or invalid (with a warning in the log) if the mesh cannot be used.
softbody3d_handle softbody3d_create(context &ctx, const softbody3d_desc &desc);

/// Edges of a sheet of cloth, combinable: `cloth3d_top | cloth3d_left`.
enum cloth3d_edge : u8 {
  cloth3d_top = 1,    ///< The first row (row 0).
  cloth3d_bottom = 2, ///< The last row.
  cloth3d_left = 4,   ///< The first column (column 0).
  cloth3d_right = 8,  ///< The last column.
};

/// Describes a sheet of cloth for cloth3d_create(): a rectangular grid of
/// `columns` x `rows` cells lying in its own x–y plane (upright like a flag or a
/// curtain), centred on `position`. The vertex at row `r`, column `c` has index
/// `r * (columns + 1) + c`; row 0 is the top edge, column 0 the left edge (the -x
/// side). Turn it with `rotation.x = 90` to lay it flat (a tablecloth).
struct cloth3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Centre of the sheet.
  vec3 rotation{0.0f, 0.0f, 0.0f}; ///< Rotation, degrees, in the same order as njin::transform3d.
  vec2 size{2.0f, 2.0f};           ///< Width (x) and height (y).
  i32 columns = 20;                ///< Cells across, 1..512.
  i32 rows = 20;                   ///< Cells down, 1..512.
  f32 mass = 0.5f;                 ///< Mass of the whole sheet, kg.
  f32 stiffness = 1.0f;            ///< Stiffness of the threads, 0 (stretchy) .. 1 (does not stretch).
  f32 bend = 0.05f;                ///< Stiffness against folding, 0 (silk) .. 1 (cardboard).
  f32 damping = 0.1f;              ///< Damping: the velocity shrinks by this fraction each second.
  f32 friction = 0.5f;             ///< Friction, 0..1.
  f32 drag = 1.0f;                 ///< How much the air drags on it, as njin::softbody3d_desc::drag.
  /// Thickness, world units: the vertices keep this far from other bodies'
  /// surfaces, so the cloth does not sink into a table when drawn.
  f32 thickness = 0.02f;
  u8 pin_edges = 0;                ///< Pinned edges (njin::cloth3d_edge bits).
  const u32 *pinned = nullptr;     ///< More pinned vertices (a flag's top corner...).
  u32 pinned_count = 0;            ///< Number of elements in `pinned`.
  u64 user = 0;                    ///< The game's number, read back with softbody3d_user().
  bool collide_soft = true;        ///< As njin::softbody3d_desc::collide_soft: two cloths do not pass through each other.
  bool walkable = true;            ///< As njin::softbody3d_desc::walkable: characters can stand on a cloth lying flat.
};

/// Creates a sheet of cloth: a flag, a curtain, a cape, a tablecloth. It is a
/// soft body like the ones softbody3d_create() makes and shares the
/// `softbody3d_*` functions. It draws from both sides.
///
/// @code
/// // A flag pinned to its pole by its left edge, blowing in the wind.
/// const njin::softbody3d_handle flag = njin::cloth3d_create(
///     ctx, {.position = {1, 4, 0}, .size = {2, 1.2f}, .columns = 20, .rows = 12, .pin_edges = njin::cloth3d_left});
/// njin::softbody3d_set_wind(ctx, flag, {6, 0, 1});
/// @endcode
/// @param ctx Engine context.
/// @param desc Description of the cloth.
/// @return Handle, or invalid if `columns` or `rows` is out of range.
softbody3d_handle cloth3d_create(context &ctx, const cloth3d_desc &desc);

/// Destroys a soft body along with its model (softbody3d_model()). An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Soft body.
void softbody3d_destroy(context &ctx, softbody3d_handle handle);

/// Number of vertices of the soft body.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @return Number of vertices, 0 if the handle is invalid.
i32 softbody3d_vertex_count(const context &ctx, softbody3d_handle handle);

/// Current positions of the vertices, in world space.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @param out Array receiving the positions, or nullptr to only count.
/// @param count Number of elements in `out`.
/// @return Number of vertices (softbody3d_vertex_count()); only the first `count` are written.
i32 softbody3d_vertices(const context &ctx, softbody3d_handle handle, vec3 *out, i32 count);

/// Current normals of the vertices (length 1, averaged over the triangles that
/// share each vertex, pointing outwards or to the cloth's front), to draw the
/// soft body yourself.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @param out Array receiving the normals, or nullptr to only count.
/// @param count Number of elements in `out`.
/// @return Number of vertices; only the first `count` are written.
i32 softbody3d_normals(const context &ctx, softbody3d_handle handle, vec3 *out, i32 count);

/// The triangles of the soft body's surface, three vertex indices per triangle,
/// counter-clockwise seen from outside (from the cloth's front, its +z side
/// when made).
/// @param ctx Engine context.
/// @param handle Soft body.
/// @param out Array receiving the indices, or nullptr to only count.
/// @param count Number of elements in `out`.
/// @return Number of indices (three times the triangles); only the first `count` are written.
i32 softbody3d_indices(const context &ctx, softbody3d_handle handle, u32 *out, i32 count);

/// A model that always has the soft body's current shape, in world space: the
/// engine updates it after every physics step. Draw it with draw_model() and the
/// default transform, change its colour and textures with model_material_set().
/// The model belongs to the soft body: do not model_unload() it,
/// softbody3d_destroy() does. Cloth gets back faces too.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @return Model, or invalid if the handle is invalid. A body with many vertices
/// (over 65535, cloth counts twice) is split into several meshes of the same model.
model_handle softbody3d_model(context &ctx, softbody3d_handle handle);

/// The vertex nearest `point`, to pin or pull one spot of the soft body.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @param point Point, world space.
/// @return Vertex index, -1 if the handle is invalid.
i32 softbody3d_nearest(const context &ctx, softbody3d_handle handle, vec3 point);

/// Pins or unpins a vertex. A pinned vertex stays put (gravity, collisions and
/// springs cannot move it) until softbody3d_move_pinned() moves it.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @param vertex Vertex index.
/// @param pinned `true` to pin.
void softbody3d_pin(context &ctx, softbody3d_handle handle, i32 vertex, bool pinned);

/// Takes a pinned vertex to `position` by the end of the next physics step,
/// pulling the rest along: a cape pinned to running shoulders, a curtain drawn
/// along its rail. Call it every fixed step with the new place; stop calling and
/// the vertex stays where it got to.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @param vertex Index of a pinned vertex; a vertex that is not pinned is ignored.
/// @param position Where it should go, world space.
void softbody3d_move_pinned(context &ctx, softbody3d_handle handle, i32 vertex, vec3 position);

/// Sets the wind blowing through the soft body, as the velocity of the air
/// (units per second). Each face across the wind is pushed by `drag` and its
/// area, so cloth flaps while a ball only drifts. No wind by default (still air
/// still slows a moving soft body).
/// @param ctx Engine context.
/// @param handle Soft body.
/// @param wind Wind velocity.
void softbody3d_set_wind(context &ctx, softbody3d_handle handle, vec3 wind);

/// Pushes the whole soft body at once (impulse, kg * units per second), shared
/// evenly among its unpinned vertices: kicking a ball.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @param impulse Impulse.
void softbody3d_add_impulse(context &ctx, softbody3d_handle handle, vec3 impulse);

/// Centre of the soft body (the average of its vertices), for a camera to follow
/// or to place a sound.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @return Centre, world space; 0 if the handle is invalid.
vec3 softbody3d_position(const context &ctx, softbody3d_handle handle);

/// The game's number given to the soft body when it was made.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @return That number, or 0 if the handle is invalid.
u64 softbody3d_user(const context &ctx, softbody3d_handle handle);

/// Draws the soft body with gizmos (njin_gizmo.h) for this frame: the edges of
/// its faces, and pinned vertices as red dots. Call every frame, in any phase.
/// @param ctx Engine context.
/// @param handle Soft body.
/// @param color Colour of the edges.
void softbody3d_draw_debug(context &ctx, softbody3d_handle handle, rgba color = {0.3f, 0.9f, 1.0f, 1.0f});

/// One wheel of a njin::vehicle3d_desc.
struct vehicle3d_wheel {
  /// Centre of the wheel with the suspension fully extended (the car lifted off
  /// the ground), in the chassis' axes: x to the left, y up, z forward.
  vec3 position{0.0f, 0.0f, 0.0f};
  f32 radius = 0.35f;     ///< Radius of the wheel.
  f32 width = 0.25f;      ///< Width of the wheel.
  bool steer = false;     ///< A steering wheel (front wheels).
  bool drive = true;      ///< The engine drives this wheel.
  bool handbrake = false; ///< The handbrake locks this wheel (rear wheels).
};

/// Describes a wheeled vehicle for vehicle3d_create(). The chassis is a dynamic
/// body shaped as the box `size` (or the convex hull of `model`), its nose
/// pointing +z: a car with `rotation` 0 drives along the z axis, as a character
/// faces along `atan2(x, z)`.
struct vehicle3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f};    ///< Centre of the chassis.
  vec3 rotation{0.0f, 0.0f, 0.0f};    ///< Rotation, degrees, in the same order as njin::transform3d.
  vec3 size{1.8f, 0.6f, 4.0f};        ///< The chassis box: width (x), height (y), length (z).
  model_handle model{};               ///< Takes the chassis from this model's convex hull instead of `size`.
  vec3 scale{1.0f, 1.0f, 1.0f};       ///< Scale of `model`.
  f32 mass = 1200.0f;                 ///< Mass, kg.
  /// Centre of mass relative to the chassis' centre. Lower than the box's
  /// centre and the car is harder to roll in a sharp turn.
  vec3 center_of_mass{0.0f, -0.3f, 0.0f};
  /// The wheels. nullptr is four wheels at the bottom corners of `size`: the two
  /// front wheels steer, all four are driven (four-wheel drive), the two rear
  /// ones have the handbrake.
  const vehicle3d_wheel *wheels = nullptr;
  u32 wheel_count = 0;                ///< Number of elements in `wheels`.
  f32 wheel_radius = 0.35f;           ///< Radius of the four default wheels.
  f32 wheel_width = 0.25f;            ///< Width of the four default wheels.
  f32 suspension = 0.3f;              ///< Suspension travel, world units.
  f32 suspension_frequency = 1.5f;    ///< Suspension stiffness, oscillations per second: 1 soft, 3 sporty.
  f32 suspension_damping = 0.5f;      ///< Damping, 0 (bounces forever) .. 1 (no bounce).
  f32 max_steer = 30.0f;              ///< Largest steering angle of the steering wheels, degrees.
  /// Largest engine torque, N·m. Beyond what the tyres can grip the wheels spin,
  /// and the gearbox does not shift up while they spin.
  f32 engine_torque = 300.0f;
  f32 max_rpm = 6000.0f;              ///< Highest engine speed. The gearbox is automatic.
  f32 brake_torque = 1500.0f;         ///< Brake torque per wheel, N·m.
  f32 handbrake_torque = 4000.0f;     ///< Handbrake torque per wheel, N·m.
  f32 friction = 0.5f;                ///< Friction of the chassis (rolled over, scraping a wall), 0..1.
  u64 user = 0;                       ///< body3d_user() of the chassis.
};

/// Creates a wheeled vehicle: the chassis is a dynamic body, each wheel is a
/// suspension probing the ground (not a body), with an engine, an automatic
/// gearbox, differentials, brakes and a handbrake. Drive it with
/// vehicle3d_set_input() every fixed step.
///
/// @code
/// car = njin::vehicle3d_create(ctx, {.position = {0, 1, 0}});
/// // Every fixed step:
/// njin::vehicle3d_set_input(ctx, car, njin::axis_value(ctx, gas), njin::axis_value(ctx, steer), 0.0f,
///                           njin::action_held(ctx, handbrake) ? 1.0f : 0.0f);
/// // Draw the chassis at body3d_transform(ctx, njin::vehicle3d_body(ctx, car)), each wheel at
/// // vehicle3d_wheel_transform().
/// @endcode
/// @param ctx Engine context.
/// @param desc Description of the vehicle.
/// @return Handle, or invalid if the chassis cannot be built or there are no wheels.
vehicle3d_handle vehicle3d_create(context &ctx, const vehicle3d_desc &desc);

/// Destroys the vehicle along with its chassis. An invalid handle is ignored.
/// @param ctx Engine context.
/// @param handle Vehicle.
void vehicle3d_destroy(context &ctx, vehicle3d_handle handle);

/// Sets how the vehicle is driven for the next physics step. Kept until the next
/// call. For every kind of vehicle: a motorcycle (motorcycle3d_create()) leans
/// into turns from `steer`; a tracked vehicle (tracked3d_create()) steers with
/// its two tracks, turns on the spot when about stopped and only steering with
/// no throttle, and `handbrake` brakes.
/// @param ctx Engine context.
/// @param handle Vehicle.
/// @param throttle Throttle, -1 (reverse) .. 1 (forward). A negative throttle while rolling forward brakes first, and reverses once stopped.
/// @param steer Steering, -1 (left) .. 1 (right).
/// @param brake Brake, 0..1.
/// @param handbrake Handbrake, 0..1.
void vehicle3d_set_input(context &ctx, vehicle3d_handle handle, f32 throttle, f32 steer, f32 brake, f32 handbrake);

/// The chassis, to read its place (body3d_transform()), its velocity, or to push
/// it. Do not destroy it with body3d_destroy(): destroy the vehicle.
/// @param ctx Engine context.
/// @param handle Vehicle.
/// @return Body, or invalid if the handle is invalid.
body3d_handle vehicle3d_body(const context &ctx, vehicle3d_handle handle);

/// Number of wheels of the vehicle.
/// @param ctx Engine context.
/// @param handle Vehicle.
/// @return Number of wheels, 0 if the handle is invalid.
i32 vehicle3d_wheel_count(const context &ctx, vehicle3d_handle handle);

/// Where a wheel is and how it is turned right now (suspension, steering and
/// spin included), in world space. The transform's y axis is the axle, so it
/// draws as a cylinder (njin::shape3d_cylinder, `radius` the wheel's radius,
/// `height` its width).
/// @param ctx Engine context.
/// @param handle Vehicle.
/// @param wheel Wheel index.
/// @return Position and rotation, degrees; default if the handle or `wheel` is invalid.
transform3d vehicle3d_wheel_transform(const context &ctx, vehicle3d_handle handle, i32 wheel);

/// Whether a wheel touches the ground.
/// @param ctx Engine context.
/// @param handle Vehicle.
/// @param wheel Wheel index.
/// @return `true` if it does.
bool vehicle3d_wheel_grounded(const context &ctx, vehicle3d_handle handle, i32 wheel);

/// The engine's speed right now, for the engine sound and the gauges.
/// @param ctx Engine context.
/// @param handle Vehicle.
/// @return Revolutions per minute, 0 if the handle is invalid.
f32 vehicle3d_rpm(const context &ctx, vehicle3d_handle handle);

/// The gear engaged: -1 reverse, 0 neutral, 1 and up the forward gears.
/// @param ctx Engine context.
/// @param handle Vehicle.
/// @return Gear, 0 if the handle is invalid.
i32 vehicle3d_gear(const context &ctx, vehicle3d_handle handle);

/// Description of a motorcycle for motorcycle3d_create(): the chassis is a
/// dynamic box `size` (or the convex hull of `model`), one front wheel steering
/// on a raked fork, one driven rear wheel. The front faces +z, as
/// njin::vehicle3d_desc. The defaults are a 240 kg motorcycle (after Jolt's
/// motorcycle sample) that rides well untouched.
struct motorcycle3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f};    ///< Centre of the chassis.
  vec3 rotation{0.0f, 0.0f, 0.0f};    ///< Rotation, degrees, in the same order as njin::transform3d.
  vec3 size{0.4f, 0.6f, 0.8f};        ///< Box of the chassis: width (x), height (y), length (z).
  model_handle model{};               ///< Take the chassis from this model's convex hull instead of `size`.
  vec3 scale{1.0f, 1.0f, 1.0f};       ///< Scale of `model`.
  f32 mass = 240.0f;                  ///< Mass of bike and rider, kg.
  vec3 center_of_mass{0.0f, -0.3f, 0.0f}; ///< Centre of mass from the centre of the chassis.
  f32 wheelbase = 1.5f;               ///< Distance between the two wheels, world units.
  f32 wheel_radius = 0.31f;           ///< Wheel radius.
  f32 wheel_width = 0.05f;            ///< Wheel width.
  f32 suspension = 0.2f;              ///< Suspension travel.
  f32 caster = 30.0f;                 ///< Rake of the front fork, degrees.
  f32 max_steer = 30.0f;              ///< Largest steering angle, degrees. The engine lowers it at speed so the bike does not fall.
  f32 max_lean = 45.0f;               ///< Largest lean into a turn, degrees.
  f32 engine_torque = 150.0f;         ///< Largest engine torque, N·m.
  f32 max_rpm = 10000.0f;             ///< Largest rpm. Automatic gearbox, six gears.
  f32 brake_torque = 500.0f;          ///< Front brake torque, N·m; the rear one half of it.
  f32 friction = 0.5f;                ///< Friction of the chassis (when it falls, when it scrapes a wall), 0..1.
  u64 user = 0;                       ///< body3d_user() of the chassis.
};

/// Creates a motorcycle (Jolt's motorcycle controller): a spring holds the bike
/// upright and leans it into turns from the speed and the steering, so it does
/// not fall standing still or riding slowly. It is a njin::vehicle3d_handle:
/// ride it with vehicle3d_set_input() (the handbrake locks the rear wheel), draw
/// it with vehicle3d_body() and vehicle3d_wheel_transform(), and every
/// `vehicle3d_*` function works on it.
///
/// @code
/// bike = njin::motorcycle3d_create(ctx, {.position = {0, 1, 0}});
/// // Every fixed step: throttle, steering, brake.
/// njin::vehicle3d_set_input(ctx, bike, gas, steer, brake, 0.0f);
/// @endcode
/// @param ctx Engine context.
/// @param desc Description of the motorcycle.
/// @return Handle, or invalid if the chassis could not be made.
vehicle3d_handle motorcycle3d_create(context &ctx, const motorcycle3d_desc &desc);

/// Description of a tracked vehicle (a tank, an excavator) for
/// tracked3d_create(): the chassis is a dynamic box `size` (or the convex hull
/// of `model`), with on each side a track running over `wheels_per_side`
/// wheels, the last one of each side driving it. The defaults are a 4 t tank
/// (after Jolt's tank sample).
struct tracked3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f};    ///< Centre of the chassis.
  vec3 rotation{0.0f, 0.0f, 0.0f};    ///< Rotation, degrees, in the same order as njin::transform3d.
  vec3 size{3.4f, 1.0f, 6.4f};        ///< Box of the chassis: width (x), height (y), length (z).
  model_handle model{};               ///< Take the chassis from this model's convex hull instead of `size`.
  vec3 scale{1.0f, 1.0f, 1.0f};       ///< Scale of `model`.
  f32 mass = 4000.0f;                 ///< Mass, kg.
  vec3 center_of_mass{0.0f, -0.5f, 0.0f}; ///< Centre of mass from the centre of the chassis.
  i32 wheels_per_side = 9;            ///< Wheels on each side, 3..16: the two end ones ride higher, the middle ones carry the vehicle.
  f32 wheel_radius = 0.3f;            ///< Wheel radius.
  f32 wheel_width = 0.1f;             ///< Wheel width (the track).
  f32 suspension = 0.2f;              ///< Suspension travel of the middle wheels.
  f32 engine_torque = 500.0f;         ///< Largest engine torque, N·m.
  f32 max_rpm = 6000.0f;              ///< Largest rpm. Automatic gearbox.
  f32 brake_torque = 15000.0f;        ///< Brake torque of each track, N·m.
  f32 friction = 0.5f;                ///< Friction of the chassis, 0..1.
  u64 user = 0;                       ///< body3d_user() of the chassis.
};

/// Creates a tracked vehicle (Jolt's tracked controller): it turns by running
/// its two tracks at different speeds, and on the spot when they run opposite
/// ways. It is a njin::vehicle3d_handle: vehicle3d_set_input() drives it like a
/// car (steering alone while stopped turns it on the spot),
/// vehicle3d_set_tracks() drives each track, vehicle3d_track_speed() gives the
/// track speed to scroll its texture.
/// @param ctx Engine context.
/// @param desc Description of the vehicle.
/// @return Handle, or invalid if the chassis could not be made.
vehicle3d_handle tracked3d_create(context &ctx, const tracked3d_desc &desc);

/// Drives the two tracks of a tracked vehicle directly for the next physics
/// step, kept until the next call: each -1 (full reverse) .. 1 (full forward).
/// `left = 1, right = -1` turns right on the spot. Ignored for a vehicle without tracks.
/// @param ctx Engine context.
/// @param handle Tracked vehicle (tracked3d_create()).
/// @param left Left track (the +x side).
/// @param right Right track (the -x side).
/// @param brake Brake, 0..1.
void vehicle3d_set_tracks(context &ctx, vehicle3d_handle handle, f32 left, f32 right, f32 brake);

/// Speed of one track, to scroll the track's texture as fast as the vehicle moves.
/// @param ctx Engine context.
/// @param handle Tracked vehicle.
/// @param side 0 is the left track (+x), 1 the right one (-x).
/// @return World units per second, negative when the track runs backwards; 0 if not a tracked vehicle.
f32 vehicle3d_track_speed(const context &ctx, vehicle3d_handle handle, i32 side);

/// Draws the vehicle with gizmos (njin_gizmo.h) for this frame: the chassis box,
/// each wheel as a circle with its axle, the suspension from where it is fixed
/// to the wheel's centre, and where wheels touch the ground (yellow). Call every
/// frame, in any phase.
/// @param ctx Engine context.
/// @param handle Vehicle.
/// @param color Colour of the chassis and wheels.
void vehicle3d_draw_debug(context &ctx, vehicle3d_handle handle, rgba color = {1.0f, 0.6f, 0.2f, 1.0f});

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
