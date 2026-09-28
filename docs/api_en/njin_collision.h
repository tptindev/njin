#pragma once
#include "_comps.h"
#include "_math.h"
#include "_types.h"
#include <entt/entity/entity.hpp>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_collision
/// @{

/// All collision layers. Default value of `collider::mask`.
inline constexpr u32 layer_all = 0xFFFFFFFFu;

/// Bit of collision layer number `n` (0..31), to name layers readably:
/// `constexpr u32 layer_enemy = njin::layer_bit(2);`.
/// @param n Layer index, 0..31.
/// @return `1 << n`.
constexpr u32 layer_bit(i32 n) { return 1u << (u32)n; }

/// Shape of a collider.
enum collider_shape {
  collider_box,    ///< Axis-aligned rectangle of size `size`. Does not rotate with the transform.
  collider_circle, ///< Circle of radius `radius`.
  /// Every non-empty tile of the njin::tilemap on the same entity is an obstacle. Used by
  /// collision_move(), queries and raycasts; produces no collision events.
  collider_tiles,
};

/// Collision shape of an entity. Needs a transform on the same entity.
///
/// The engine's collision module, in `phase_post_update` (after the transforms
/// of child entities have been computed), finds every pair of overlapping colliders and sends
/// the events njin::collision_enter, njin::collision_stay, njin::collision_exit through
/// events(). The game receives them with `events(ctx).sink<...>().connect<...>()`.
///
/// Two colliders A and B are only considered when **both directions** allow it:
/// `(A.mask & B.layer) != 0` and `(B.mask & A.layer) != 0`, like Box2D.
///
/// The center of the shape is `transform.pos` plus `offset`; `offset`, `size` and
/// `radius` are multiplied by `transform.scale`, and `offset` rotates with `transform.rot`
/// (so a hitbox attached to a sword tip follows the blade), but the box shape
/// itself is always axis-aligned.
struct collider {
  collider_shape shape = collider_box; ///< Shape.
  vec2 size{16.0f, 16.0f}; ///< Size of the box shape.
  f32 radius = 8.0f;       ///< Radius of the circle shape.
  vec2 offset{};           ///< Offset of the center from `transform.pos`.
  u32 layer = layer_bit(0); ///< The layers this collider belongs to.
  u32 mask = layer_all;     ///< The layers this collider collides with.
  /// Only reports overlap, does not block the way: pickup area, damage area, screen
  /// transition door. collision_move() passes through triggers; raycasts skip them by default.
  bool trigger = false;
  bool enabled = true; ///< Temporarily disable without removing the component. Disabling produces `exit`.
  /// One-way platform (box shape only): collision_move() is only blocked when falling from
  /// above onto its top face; moving sideways or jumping up from below passes through.
  bool one_way = false;
};

/// Tag: the entity is carried by a moving platform when standing on it, and pushed by the platform
/// when the platform runs into it. See collision_move_platform(). njin::platformer_body and
/// njin::topdown_body do not need this tag.
struct platform_rider {};

/// Two colliders start overlapping.
///
/// Each pair sends **two** events, one for each side, so a handler only needs to look at `self`:
/// @code
/// void on_enter(njin::collision_enter &e) {
///   if (reg.all_of<bullet>(e.self) && reg.all_of<enemy>(e.other)) ...
/// }
/// @endcode
/// Events are dispatched after `phase_post_update`, so destroying entities in a handler is
/// safe. But the events of the whole frame are already queued: an earlier handler may have
/// destroyed `self` or `other`, so check both with `registry.valid()` first.
struct collision_enter {
  entt::entity self = entt::null;  ///< Entity receiving the event.
  entt::entity other = entt::null; ///< The other entity.
  bool trigger = false; ///< One of the two is a trigger.
};

/// Two colliders are still overlapping, sent every frame after the `enter` frame.
struct collision_stay {
  entt::entity self = entt::null;  ///< Entity receiving the event.
  entt::entity other = entt::null; ///< The other entity.
  bool trigger = false; ///< One of the two is a trigger.
};

/// Two colliders stop overlapping: separated, disabled, removed, or destroyed.
///
/// Only sent to the side that is still alive; `other` may already be destroyed, check with
/// `registry.valid(e.other)` before reading its components.
struct collision_exit {
  entt::entity self = entt::null;  ///< Entity receiving the event.
  entt::entity other = entt::null; ///< The other entity, may already be destroyed.
  bool trigger = false; ///< One of the two is a trigger.
};

/// Rectangle bounding a box or circle collider, in the world.
/// @param tr Entity transform.
/// @param col Collider.
/// @return Bounding rectangle. For `collider_tiles` an empty shape at `tr.pos`.
inline rect collider_bounds(const transform &tr, const collider &col) {
  const f32 s = tr.scale < 0.0f ? -tr.scale : tr.scale;
  const vec2 center = tr.pos + rotate(col.offset * s, tr.rot);
  if (col.shape == collider_circle)
    return rect_from_center(center, vec2{col.radius, col.radius} * (2.0f * s));
  if (col.shape == collider_box)
    return rect_from_center(center, col.size * s);
  return rect{tr.pos, {}};
}

/// Result of collision_move().
struct collision_move_result {
  vec2 moved{};  ///< The displacement actually applied.
  bool hit_x = false; ///< Blocked on the horizontal axis.
  bool hit_y = false; ///< Blocked on the vertical axis. `hit_y && delta.y > 0` means standing on the ground.
  entt::entity other_x = entt::null; ///< What blocked on the horizontal axis.
  entt::entity other_y = entt::null; ///< What blocked on the vertical axis.
  /// After moving, the entity is standing on ground, a slope or a platform (no more
  /// than half a pixel below).
  bool grounded = false;
  entt::entity ground = entt::null; ///< What it is standing on, if `grounded`.
  bool on_slope = false;       ///< Standing on a slope tile.
  bool ground_one_way = false; ///< Standing on a one-way platform (tile or collider).
};

/// Options of collision_move().
struct collision_move_opts {
  /// Ignore one-way platforms (`tile_one_way` tiles and `one_way` colliders): to drop
  /// down off a platform.
  bool drop_through = false;
  /// When greater than 0 and the entity is not moving up: if after moving there is ground
  /// within this distance below, pull the entity down to touch it. Keeps the character stuck to
  /// the slope surface when going downhill instead of bouncing. Usually `|dx| + 2` when
  /// standing on the ground, 0 when in the air.
  f32 snap_down = 0.0f;
  /// Only compute, do not write the new position to the transform: to probe walls or ground.
  bool test_only = false;
};

/// Moves an entity that has a collider, stopping when it hits a **non-trigger**
/// collider (including tiles of `collider_tiles`), then writes the new position to the transform.
///
/// Like tilemap_move(): moves the horizontal axis first then the vertical, so it slides along
/// walls. A circle is moved as its bounding box. Only blocked by colliders
/// that both layer/mask directions allow, and ignores the colliders of the entity's direct
/// children (njin::child_of) so a weapon does not block its owner.
///
/// Each axis is swept continuously, so no matter how long the step it does not pass through thin
/// walls. Obstacles already overlapping the entity beforehand are ignored, so a stuck entity
/// can still walk out. Run it in `phase_fixed_update` for stability.
/// @param ctx Engine context.
/// @param entity Entity with a transform and a box or circle collider.
/// @param delta Desired displacement.
/// @return The actual displacement and the blocker on each axis.
collision_move_result collision_move(njin_ctx &ctx, entt::entity entity, vec2 delta);

/// Like the previous overload, with options: one-way platforms, slope sticking, probe only.
///
/// **Tilemap tiles** follow tilemap::shapes: `tile_none` does not block, `tile_one_way`
/// only blocks when falling from above onto its surface, the `tile_slope_*` tiles support the entity
/// along the slope surface at the **bottom-center point** of the box. Walking sideways up a slope to
/// a solid tile as tall as the slope's top is seamless. The tall vertical face of a slope tile blocks like a wall.
/// @param ctx Engine context.
/// @param entity Entity with a transform and a box or circle collider.
/// @param delta Desired displacement.
/// @param opts Options.
/// @return The actual displacement, the blockers, and the ground state.
collision_move_result collision_move(njin_ctx &ctx, entt::entity entity, vec2 delta,
                                     const collision_move_opts &opts);

/// Moves a platform (an entity with a box collider, usually `one_way` or an obstacle)
/// **without being blocked**, and carries whatever stands on it.
///
/// Entities standing on the platform surface (bottom touching the platform surface, overlapping horizontally) that have
/// njin::platformer_body, njin::topdown_body or njin::platform_rider are
/// moved by the same displacement using collision_move(), so they are still blocked by walls. A platform that is not
/// one-way also pushes those entities out when it runs into them. Call it in
/// `phase_fixed_update`, **before** the character moves. njin::path_mover
/// calls this for you.
/// @param ctx Engine context.
/// @param platform Platform entity, with a transform and a collider.
/// @param delta Displacement of the platform.
void collision_move_platform(njin_ctx &ctx, entt::entity platform, vec2 delta);

/// Finds every entity whose collider overlaps the rectangle `area`.
/// @param ctx Engine context.
/// @param area Area to search, in the world.
/// @param out Receives the found entities (appended to the end, not cleared first). May be null.
/// @param mask Only consider colliders with `layer & mask != 0`.
/// @param include_triggers Whether to consider trigger colliders.
/// @return Number of entities found.
i32 collision_overlap_rect(const njin_ctx &ctx, rect area,
                           std::vector<entt::entity> *out = nullptr,
                           u32 mask = layer_all, bool include_triggers = true);

/// Like collision_overlap_rect() with a circle: blast area, sight range.
/// @param ctx Engine context.
/// @param area Area to search.
/// @param out Receives the found entities. May be null.
/// @param mask Only consider colliders with `layer & mask != 0`.
/// @param include_triggers Whether to consider trigger colliders.
/// @return Number of entities found.
i32 collision_overlap_circle(const njin_ctx &ctx, circle area,
                             std::vector<entt::entity> *out = nullptr,
                             u32 mask = layer_all, bool include_triggers = true);

/// Like collision_overlap_rect() with a point: what the mouse is pointing at.
/// @param ctx Engine context.
/// @param point Point, in the world (use scr2w() for the mouse position).
/// @param out Receives the found entities. May be null.
/// @param mask Only consider colliders with `layer & mask != 0`.
/// @param include_triggers Whether to consider trigger colliders.
/// @return Number of entities found.
i32 collision_overlap_point(const njin_ctx &ctx, vec2 point,
                            std::vector<entt::entity> *out = nullptr,
                            u32 mask = layer_all, bool include_triggers = true);

/// Result of collision_raycast().
struct raycast_hit {
  bool hit = false;   ///< Whether anything was hit.
  entt::entity entity = entt::null; ///< Entity that was hit (the tilemap if a tile was hit).
  vec2 point{};       ///< Hit point, in the world.
  vec2 normal{};      ///< Surface normal at the hit point, length 1.
  f32 distance = 0.0f; ///< Distance from `from` to the hit point.
};

/// Casts a ray from `from` to `to`, returning the **nearest** thing hit: monster
/// sight, instant bullets, lasers, ground checks.
///
/// A ray that starts inside a collider hits immediately at `from`.
/// @param ctx Engine context.
/// @param from Start point.
/// @param to End point.
/// @param mask Only consider colliders with `layer & mask != 0`.
/// @param include_triggers Whether to consider trigger colliders. Off by default.
/// @param ignore Entity to skip, usually the shooter itself.
/// @return What was hit, or `hit == false`.
raycast_hit collision_raycast(const njin_ctx &ctx, vec2 from, vec2 to,
                              u32 mask = layer_all, bool include_triggers = false,
                              entt::entity ignore = entt::null);

/// Whether they can see each other: the ray from `from` to `to` hits no obstacle.
///
/// Used for monster sight. `tile_none` tiles and triggers do not block; one-way
/// platforms only block when looking from above.
/// @param ctx Engine context.
/// @param from Viewpoint.
/// @param to Point to see.
/// @param mask Only consider colliders with `layer & mask != 0`.
/// @param ignore Entity to skip, usually the viewer itself.
/// @return `true` if nothing blocks between the two points.
inline bool collision_line_of_sight(const njin_ctx &ctx, vec2 from, vec2 to,
                                    u32 mask = layer_all, entt::entity ignore = entt::null) {
  return !collision_raycast(ctx, from, to, mask, false, ignore).hit;
}

/// Draws the outline of every box and circle collider in `phase_render`, over sprites: green
/// for obstacles, yellow for triggers. For debugging hitboxes.
/// @param ctx Engine context.
/// @param on On or off.
void collision_set_debug(njin_ctx &ctx, bool on);

/// Cell size of the grid used to find collision pairs, in world units. Default 64. Should be close to
/// the common collider size in the game; only affects speed, does not change results.
/// @param ctx Engine context.
/// @param size Cell size, greater than 0.
void collision_set_cell_size(njin_ctx &ctx, f32 size);
/// @}
} // namespace njin
