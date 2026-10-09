#pragma once
#include "_math.h"
#include "_types.h"
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_nav3d
/// @{

/// How navmesh3d_create() builds a 3D navmesh: the size of the agents that walk
/// on it and how fine the build is.
///
/// A navmesh is the polygons covering where static geometry can be walked on
/// (floors, ramps, stairs, terrain), already pulled in by `agent_radius` from
/// walls and obstacles. It is split into square tiles `tile_size` metres a side,
/// so one area can be rebuilt (navmesh3d_rebuild()) without rebuilding the whole
/// map.
///
/// Path points and agent positions sit on the ground: their height follows the
/// navmesh's detail mesh, and over terrain added with navmesh3d_add_terrain()
/// exactly terrain3d_height(). Over other geometry (models, slanted boxes) they can
/// be a few cm off.
struct navmesh3d_desc {
  f32 agent_radius = 0.4f;  ///< Agent radius, metres: walkable area stays this far from walls.
  f32 agent_height = 1.8f;  ///< Agent height: only fits under ceilings higher than this.
  f32 agent_climb = 0.4f;   ///< Highest step it can climb, metres.
  f32 max_slope = 45.0f;    ///< Steepest walkable slope, degrees.
  /// Cell size when building, metres, horizontally: smaller puts polygon edges
  /// closer to walls but builds longer. About a third of `agent_radius` is usual.
  f32 cell_size = 0.15f;
  f32 cell_height = 0.1f;   ///< Cell size when building, vertically, metres.
  f32 tile_size = 16.0f;    ///< Side of each square tile of the navmesh, metres.
  f32 region_min = 8.0f;    ///< Walkable patches smaller than this (in cells, per side) are dropped: a spot on top of a table.
  f32 edge_max_error = 1.3f; ///< Polygon edges stray at most this many cells from the real edge.
  /// One corner (smallest x, y, z) and the other corner of the build area. Equal
  /// corners (the default) take it from the geometry added at the first build.
  vec3 bounds_min{0.0f, 0.0f, 0.0f};
  vec3 bounds_max{0.0f, 0.0f, 0.0f}; ///< See `bounds_min`.
  i32 max_agents = 128;     ///< Most agents walking at once (nav3d_agent_add()).
};

/// Creates an empty navmesh. Add geometry with navmesh3d_add_mesh(),
/// navmesh3d_add_model(), navmesh3d_add_box(), navmesh3d_add_terrain(), then build
/// it with navmesh3d_build().
/// @param ctx The engine context.
/// @param desc How to build it.
/// @return Handle, or an invalid handle if `desc` is wrong (a warning says why).
navmesh3d_handle navmesh3d_create(context &ctx, const navmesh3d_desc &desc = {});

/// Destroys the navmesh and all its agents. An invalid handle is ignored.
/// @param ctx The engine context.
/// @param handle Navmesh.
void navmesh3d_destroy(context &ctx, navmesh3d_handle handle);

/// Adds a triangle mesh (in the world) as geometry: floors, walls, stairs. It is
/// copied.
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param positions The vertices.
/// @param vertex_count Number of vertices.
/// @param indices Three indices per triangle; nullptr is every three consecutive vertices one triangle.
/// @param index_count Number of indices, a multiple of 3.
void navmesh3d_add_mesh(context &ctx, navmesh3d_handle handle, const vec3 *positions, u32 vertex_count,
                        const u32 *indices = nullptr, u32 index_count = 0);

/// Adds the triangles of a model, placed at a position, rotation (degrees) and
/// scale as draw_model() does. The model is read again at each build, so it must
/// still be alive then.
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param model Model.
/// @param position Position.
/// @param rotation Rotation, degrees, as njin::transform3d::rotation.
/// @param scale Scale.
void navmesh3d_add_model(context &ctx, navmesh3d_handle handle, model_handle model, vec3 position,
                         vec3 rotation = {0.0f, 0.0f, 0.0f}, vec3 scale = {1.0f, 1.0f, 1.0f});

/// Adds a box: a platform, a table, an obstacle, a wall.
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param center Centre.
/// @param size Size.
/// @param rotation Rotation, degrees.
void navmesh3d_add_box(context &ctx, navmesh3d_handle handle, vec3 center, vec3 size,
                       vec3 rotation = {0.0f, 0.0f, 0.0f});

/// Adds a terrain (njin_world3d.h). Its heights are read again at each build, so
/// after terrain3d_edit() only the edited area needs navmesh3d_rebuild().
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param terrain Terrain.
void navmesh3d_add_terrain(context &ctx, navmesh3d_handle handle, terrain3d_handle terrain);

/// Adds a shortcut agents can take even though the ground does not join: a jump
/// over a gap, a ladder, a drop from a ledge. Both ends must lie on the navmesh
/// (no further than `radius` from it).
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param from Start.
/// @param to End.
/// @param both_ways Walkable in the other direction too.
/// @param radius How far around each end to look for the navmesh, metres.
void navmesh3d_add_link(context &ctx, navmesh3d_handle handle, vec3 from, vec3 to, bool both_ways = true,
                        f32 radius = 0.5f);

/// Drops all geometry and links added (the built navmesh stays until the next build).
/// @param ctx The engine context.
/// @param handle Navmesh.
void navmesh3d_clear_geometry(context &ctx, navmesh3d_handle handle);

/// Builds the whole navmesh from the geometry added. Takes from a few
/// milliseconds to a few seconds depending on the map size and `cell_size`: call
/// it while loading a level, not every frame.
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @return `true` if there is at least one walkable area.
bool navmesh3d_build(context &ctx, navmesh3d_handle handle);

/// Rebuilds the tiles touching the area `[min, max]` from the current geometry:
/// after editing a terrain, opening a door, placing a new obstacle (add it with
/// navmesh3d_add_box()). Walking agents find their path again.
/// @param ctx The engine context.
/// @param handle A built navmesh.
/// @param min Small corner of the area.
/// @param max Large corner of the area.
/// @return Number of tiles rebuilt.
i32 navmesh3d_rebuild(context &ctx, navmesh3d_handle handle, vec3 min, vec3 max);

/// Finds a path from `from` to `to`: the corner points, the first the point on
/// the navmesh nearest `from`, the last the point nearest `to`. If `to` cannot be
/// reached the path stops at the reachable point nearest it (compare the last
/// point with `to` to know). Over terrain, a segment that would cut through a hill
/// gets points in between so the path follows the ground (within 5 cm).
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param from Start.
/// @param to Destination.
/// @param out Receives the points (cleared first).
/// @return `true` if a path was found (even one only getting near `to`); `false`
/// if `from` or `to` is not near the navmesh.
bool navmesh3d_path(const context &ctx, navmesh3d_handle handle, vec3 from, vec3 to, std::vector<vec3> &out);

/// The point on the navmesh nearest `p`, searched in a box of half size `extents`
/// around `p`.
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param p Point.
/// @param out Receives the point found.
/// @param extents Half size of the search box, metres.
/// @return `true` if there is one.
bool navmesh3d_nearest(const context &ctx, navmesh3d_handle handle, vec3 p, vec3 &out,
                       vec3 extents = {2.0f, 4.0f, 2.0f});

/// Result of navmesh3d_raycast().
struct nav3d_ray {
  bool hit = false;   ///< Met an edge of the navmesh (a wall) before reaching the end.
  vec3 point{};       ///< Where it stopped: at the edge, or at the end if nothing was met.
  vec3 normal{};      ///< Horizontal normal of the edge met.
  f32 fraction = 1.0f; ///< Part of the way travelled, 0..1.
};

/// Walks straight along the navmesh surface from `from` towards `to` and stops at
/// the first edge: whether two points see each other along the ground, how far a
/// dash can go.
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param from Start (near the navmesh).
/// @param to End.
/// @return The result; on a miss `point` is `to` projected onto the navmesh.
nav3d_ray navmesh3d_raycast(const context &ctx, navmesh3d_handle handle, vec3 from, vec3 to);

/// A random point on the navmesh (walkable areas weighted by their area).
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param out Receives the point.
/// @return `true` if there is one.
bool navmesh3d_random_point(context &ctx, navmesh3d_handle handle, vec3 &out);

/// A random point on the navmesh, no further than `radius` from `center` and
/// reachable from it: where a monster wanders around its lair.
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param center Centre.
/// @param radius Radius, metres.
/// @param out Receives the point.
/// @return `true` if there is one.
bool navmesh3d_random_point_near(context &ctx, navmesh3d_handle handle, vec3 center, f32 radius, vec3 &out);

/// Draws the navmesh polygons with gizmos (njin_gizmo.h) this frame: polygon
/// edges, outer edges stronger, and the links.
/// @param ctx The engine context.
/// @param handle Navmesh.
/// @param color Colour.
void navmesh3d_draw_debug(context &ctx, navmesh3d_handle handle, rgba color = {0.2f, 0.8f, 1.0f, 1.0f});

/// An agent walking on the navmesh with the others: it finds its own path, steers
/// round the others and keeps apart from those standing close.
struct nav3d_agent_desc {
  vec3 position{};          ///< Where it starts (pulled onto the navmesh).
  f32 radius = 0.4f;        ///< Radius, metres.
  f32 height = 1.8f;        ///< Height, metres.
  f32 max_speed = 3.5f;     ///< Top speed, metres per second.
  f32 max_accel = 10.0f;    ///< Top acceleration, metres per second squared.
  f32 separation = 2.0f;    ///< How hard it keeps apart from agents next to it. 0 is off.
  bool avoid = true;        ///< Steers round other agents on its way.
  /// It has arrived (nav3d_agent_arrived()) within this many metres of the target.
  /// 0 is `radius`.
  f32 arrive_distance = 0.0f;
  /// A character (njin_physics3d.h) this agent drives: each frame the agent's
  /// velocity becomes the character's horizontal velocity (character3d_set_velocity();
  /// the engine makes it fall by physics3d_gravity() when it is not on the ground),
  /// and where the character really got to (pushed, blocked) becomes the agent's
  /// position. The game should no longer set this character's velocity. Invalid
  /// is an agent that moves by itself, with no physics collisions.
  character3d_handle character{};
};

/// Adds an agent. It stands still until it has a target (nav3d_agent_set_target()).
/// The engine moves every agent in `phase_post_update` by delta().
/// @param ctx The engine context.
/// @param navmesh A built navmesh.
/// @param desc The agent.
/// @return Handle, or an invalid handle if the navmesh is not built, already has
/// `max_agents`, or `position` is not near it.
nav3d_agent_handle nav3d_agent_add(context &ctx, navmesh3d_handle navmesh, const nav3d_agent_desc &desc);

/// Removes an agent. An invalid handle is ignored.
/// @param ctx The engine context.
/// @param agent Agent.
void nav3d_agent_remove(context &ctx, nav3d_agent_handle agent);

/// Sets the target: the agent finds a path and walks there (the target is pulled
/// to the nearest point on the navmesh). Calling it again as the target moves
/// (chasing the player) is fine, and costs nothing extra while the target barely
/// moves.
/// @param ctx The engine context.
/// @param agent Agent.
/// @param target Target.
/// @return `false` if the target is not near the navmesh.
bool nav3d_agent_set_target(context &ctx, nav3d_agent_handle agent, vec3 target);

/// Drops the target: the agent stops (and still keeps apart from others).
/// @param ctx The engine context.
/// @param agent Agent.
void nav3d_agent_stop(context &ctx, nav3d_agent_handle agent);

/// Puts the agent somewhere else at once (a teleport), dropping its target.
/// @param ctx The engine context.
/// @param agent Agent.
/// @param position New position (pulled onto the navmesh).
/// @return `false` if that position is not near the navmesh.
bool nav3d_agent_teleport(context &ctx, nav3d_agent_handle agent, vec3 position);

/// The agent's current position (on the navmesh surface).
/// @param ctx The engine context.
/// @param agent Agent.
/// @return Position, or (0, 0, 0) if the handle is invalid.
vec3 nav3d_agent_position(const context &ctx, nav3d_agent_handle agent);

/// The agent's current velocity: where it is heading, to turn it and pick an
/// animation.
/// @param ctx The engine context.
/// @param agent Agent.
/// @return Velocity, metres per second.
vec3 nav3d_agent_velocity(const context &ctx, nav3d_agent_handle agent);

/// Whether the agent has reached its target (no further than `arrive_distance`).
/// @param ctx The engine context.
/// @param agent Agent.
/// @return `true` if it has arrived, `false` while walking or without a target.
bool nav3d_agent_arrived(const context &ctx, nav3d_agent_handle agent);

/// Number of agents on a navmesh.
/// @param ctx The engine context.
/// @param navmesh Navmesh.
/// @return Number of agents.
i32 nav3d_agent_count(const context &ctx, navmesh3d_handle navmesh);
/// @}
} // namespace njin
