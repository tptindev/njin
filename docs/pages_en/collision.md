# Collision {#collision}

@ref math has geometry functions for testing two shapes yourself. This page is the layer above: attach a
njin::collider to an entity, and the engine finds the touching pairs by itself and tells the game with events. Along
with that come movement with obstacles, area queries and raycasts.

This is **not** a physics simulation: there are no forces, mass, bouncing or stacking. It is for
the things action games need: bullets hitting monsters, picking up items, damage zones, blocking walls, monsters seeing
the player.

@include collision.cpp

## Collider

| Field | Meaning |
|---|---|
| `shape` | `collider_box` (an axis-aligned box), `collider_circle`, or `collider_tiles` |
| `size`, `radius` | Box size, circle radius |
| `offset` | Offset of the center from `transform.pos` |
| `layer`, `mask` | Which layer it belongs to, which layers it collides with |
| `trigger` | Only reports overlap, does not block the way |
| `enabled` | Turn it off temporarily without removing the component |

`offset`, `size` and `radius` are multiplied by `transform.scale`; `offset` rotates with `transform.rot`.
The box itself is always axis-aligned, it does not rotate.

A collider can sit on a child entity (njin::child_of), for example a hitbox at the tip of a sword: the collision module
runs after the hierarchy module so it sees this frame's correct position.

## Layers and masks

Each collider belongs to one or more layers (`layer`, up to 32, named with njin::layer_bit()) and
chooses which layers it collides with (`mask`). Two colliders A and B are only considered when **both directions**
allow it:

```
(A.mask & B.layer) != 0  and  (B.mask & A.layer) != 0
```

For example, the player's bullet has `mask = layer_enemy | layer_wall`: it never hits the player,
even if the player has `mask = layer_all`. This is also Box2D's rule.

## Events

Every frame, in `phase_post_update`, the engine finds every pair of box or circle colliders that overlap
and queues events through njin::events():

| Event | When |
|---|---|
| njin::collision_enter | The first frame two colliders overlap |
| njin::collision_stay | Every frame after that, while they still overlap |
| njin::collision_exit | The frame they stop overlapping: separated, disabled, collider removed, or destroyed |

- Each pair sends **two** events, one for each side (`self` is the receiving side). A handler only needs to ask "what is `self`,
  what is `other`".
- Events are dispatched after `phase_post_update`, so destroying an entity inside a handler is safe. But
  all of the frame's events were queued beforehand: if two bullets hit the same monster, the first
  handler destroys the monster and the second event still arrives. Always check `registry.valid()` for both `self` and
  `other` at the start of the handler.
- `exit` is only sent to the side that is still alive. `other` may already be destroyed: check `registry.valid()`.
- `trigger` in the event is `true` if either one is a trigger.

Finding pairs uses a uniform grid: only colliders sharing a cell are compared. The default cell size is 64, adjusted with
njin::collision_set_cell_size() to be close to your game's typical collider size. On a test machine, in a Release build,
2000 moving boxes take about 1 ms per frame.

## Movement with obstacles

njin::collision_move() moves an entity, stopping before non-trigger colliders and before the tiles
of a `collider_tiles`, then writes the new position into the transform:

```cpp
const njin::collision_move_result r = njin::collision_move(ctx, hero, velocity * dt);
if (r.hit_y && velocity.y > 0)
  on_ground = true;
if (r.hit_x)
  velocity.x = 0;
```

- It moves along the horizontal axis first, then the vertical one, so it slides along walls.
- Each axis is **swept continuously**: no matter how long the step, it never passes through a thin wall.
- Obstacles already overlapping the entity are ignored, so a stuck entity can still walk out.
- The colliders of direct child entities are ignored, so a weapon does not block its owner.
- A circle moves as its bounding box.

## Tilemap as an obstacle

Attach a `collider_tiles` collider to an entity with a njin::tilemap: every non-empty tile becomes an obstacle
for njin::collision_move(), queries and raycasts, with `layer`/`mask` like a normal collider.

```cpp
reg.emplace<njin::collider>(level, njin::collider{.shape = njin::collider_tiles, .layer = layer_wall});
```

Tilemap tiles do not generate collision events. njin::tilemap_move() is still usable when you only need collision with
one tilemap.

## Queries and raycasts

| Function | Used for |
|---|---|
| njin::collision_overlap_rect() | The hit area of a sword slash |
| njin::collision_overlap_circle() | Explosion area, detection range |
| njin::collision_overlap_point() | What the mouse is pointing at (use njin::scr2w()) |
| njin::collision_raycast() | Line of sight, instant bullets, lasers, checking for standing on the ground |

Queries run immediately when called, on the current positions, and walk through every collider: fast with a few hundred
colliders, but do not call them thousands of times per frame. A raycast returns the **nearest** thing hit, ignores
triggers by default, and has an `ignore` parameter to skip the shooter.

## Crowds: nearest neighbours and pushing apart

Colliders suit a few hundred things. A swarm of a few thousand zombies or a crowd needs something else:
they need no events, only to **not overlap** and to **know who is near**. That is the job of
njin::spatial_index, in `njin_spatial.h`:

| Function | Used for |
|---|---|
| njin::spatial_build() | Rebuilding the index every frame from njin::spatial_item circles |
| njin::spatial_nearest() | The k items nearest a point: auto-aim, finding a partner, nearest enemy |
| njin::spatial_separate() | Pushing overlapping items apart, each counting only its k nearest touching ones |

@include spatial.cpp

A `fixed` item is not pushed, others have to go round it: someone sitting, a guard at a post.
Items of the same njin::spatial_item::group do not push each other, so a group can stand close. Each
item only adds the pushes of its k nearest touching ones, so in a dense clump it is not thrown out;
what overlap remains melts over a few frames. The denser the clump, the more items each query has
to look at, so it is still slower than an even spread.

njin::spatial_desc::kind picks the structure underneath. Both return **the same results** and differ
only in speed, so switch between them and measure in your game:

| Kind | Faster when |
|---|---|
| njin::spatial_grid (default) | Items of one size, spread fairly evenly over a screen: swarms, crowds, bullets. Builds 3 to 4 times faster than the quadtree. |
| njin::spatial_quadtree | A very wide map with items gathered in a few clumps, and queries with a large radius. |

Measured on the crowd game (radius 5, spread evenly): with 50,000 people, the whole simulation
step including building the index and pushing apart takes 13.9 ms with the grid, 26.3 ms with the
quadtree. On a 100,000 x 65,000 map with 20 clumps, 5000 queries for the 8 nearest within radius
180 take 10.7 ms with the grid, 7.2 ms with the quadtree.

## Debugging

njin::collision_set_debug() draws an outline around every box and circle collider: green is an obstacle, yellow is a
trigger, gray is disabled.
