# 3D pathfinding and crowds {#nav_3d}

This page lets computer-controlled characters find their own way around a 3D level: round walls, up ramps, onto
platforms, across gaps; and walk as a crowd without bumping into each other. Everything is declared in
`njin_nav3d.h`, built on Recast and Detour (the pathfinding library of a great many 3D games), inside the engine.

Read first: @ref graphics_3d (begin_3d(), 3D physics). A 2D game on a tile grid uses `nav_grid` from
@ref topdown, which is much simpler.

## What a navmesh is

A navmesh is the polygons covering **where one can walk** in a level: floors, ramps, platform tops, already pulled
in from walls and obstacles by the character's radius. Finding a path on it is much faster than on a grid of
tiles, and the path that comes out is straight lines bending at the corners of obstacles, not a zigzag along
tiles.

The engine builds the navmesh from the static geometry the game gives it, for the size of the character:

| Field of njin::navmesh3d_desc | Meaning |
|---|---|
| `agent_radius` | Walkable area stays this many metres from walls |
| `agent_height` | Only fits under ceilings higher than this |
| `agent_climb` | Highest step it can climb |
| `max_slope` | Steepest walkable slope, degrees |
| `cell_size`, `cell_height` | How fine the build is: smaller hugs walls closer, builds longer |
| `tile_size` | Side of each square tile of the navmesh, to rebuild one area at a time |

## Building a navmesh

Create a navmesh, add geometry, then build it once while loading the level:

```cpp
const njin::navmesh3d_handle nav = njin::navmesh3d_create(ctx, {.agent_radius = 0.4f});
njin::navmesh3d_add_box(ctx, nav, {0, -0.5f, 0}, {30, 1, 30});   // floor
njin::navmesh3d_add_model(ctx, nav, level_model, {0, 0, 0});     // the level's model
njin::navmesh3d_add_terrain(ctx, nav, ground);                   // a terrain from @ref world_3d
njin::navmesh3d_build(ctx, nav);
```

| Function | Adds |
|---|---|
| navmesh3d_add_box() | A box, which may be rotated: floor, wall, platform, table |
| navmesh3d_add_mesh() | A triangle mesh in the world (copied) |
| navmesh3d_add_model() | A model's triangles, placed as draw_model() does |
| navmesh3d_add_terrain() | A terrain; its heights are read again at each build |
| navmesh3d_add_link() | A shortcut where the ground does not join: a jump over a gap, a ladder, a drop from a ledge |

Triangles must face up (counter-clockwise seen from above, as every njin mesh). A build takes from a few tens of
milliseconds (a room) to a few seconds (a big map with a small `cell_size`).

When the level changes (a terrain edited, a door opened, an obstacle placed with navmesh3d_add_box()),
navmesh3d_rebuild() rebuilds the tiles in one area rather than the whole map. Walking agents find their path
again.

## Asking the navmesh

| Function | Answers |
|---|---|
| navmesh3d_path() | A path from A to B: the corner points. If B cannot be reached it stops at the point nearest B |
| navmesh3d_nearest() | The nearest point on the navmesh |
| navmesh3d_raycast() | Walks straight along the ground from A towards B and stops at the first edge: whether they see each other, how far a dash goes |
| navmesh3d_random_point() | A random point anywhere on the navmesh |
| navmesh3d_random_point_near() | A random reachable point no further than a radius from a centre: a monster wandering round its lair |
| navmesh3d_draw_debug() | Draws the polygons with gizmos, to see where the engine thinks one can walk |

```cpp
std::vector<njin::vec3> path;
if (njin::navmesh3d_path(ctx, nav, guard_pos, player_pos, path))
  for (size_t i = 0; i + 1 < path.size(); i++)
    njin::gizmo_line3d(ctx, path[i], path[i + 1], njin::colors::yellow);
```

Path points and agent positions sit on the ground. Over terrain added with navmesh3d_add_terrain(), the height is
exactly terrain3d_height(), and a segment that would cut through a hill gets points in between so the path follows
the ground (within 5 cm). Over other geometry (models, slanted boxes) the height follows the navmesh's detail mesh and
can be a few cm off; for an exact height cast a ray downwards, or let the agent drive a physics character (below).

## Crowds

An agent (nav3d_agent_add()) is a character that finds its own path and walks by itself: the game only sets the
target. Agents on the same navmesh steer round each other on the way and keep apart when standing close, so a
pack of monsters chasing the player does not bunch into one lump. The engine moves them in `phase_post_update`
by delta().

```cpp
const njin::nav3d_agent_handle orc = njin::nav3d_agent_add(ctx, nav, {.position = spawn, .max_speed = 4.0f});
njin::nav3d_agent_set_target(ctx, orc, player_pos);    // calling it every frame as the target moves is fine
const njin::vec3 p = njin::nav3d_agent_position(ctx, orc);
const njin::vec3 v = njin::nav3d_agent_velocity(ctx, orc); // which way to face, walk or run animation
if (njin::nav3d_agent_arrived(ctx, orc))
  attack();
```

nav3d_agent_stop() drops the target, nav3d_agent_teleport() puts it somewhere else at once, nav3d_agent_remove()
removes the agent.

**Driving a physics character.** Set `nav3d_agent_desc::character` to a njin::character3d_handle: each frame the
agent's velocity becomes the character's horizontal velocity, the character falls by gravity when it is not on
the ground, and where the character really got to (pushed, blocked by a crate) becomes the agent's position. The
game should no longer set that character's velocity itself.

## Full example

A room with walls and a platform with a ramp; eight agents walk to where the left mouse button clicks; the path
from the first one to the mouse is drawn as a preview.

@include nav3d.cpp

## From Lua

Scripts (@ref scripting) can call `njin.nav3d_path(navmesh, from, to)` (a table of points, or nil),
`njin.nav3d_set_target(agent, target)`, `njin.nav3d_stop`, `njin.nav3d_position`, `njin.nav3d_velocity` and
`njin.nav3d_arrived`, where `navmesh` and `agent` are the handle's `id` number passed over from C++ (for example
with script_set_global()).

## Limits

- The navmesh is built from static geometry. Moving things (a pushed crate, a door) do not cut it by themselves:
  add or drop the shape, then navmesh3d_rebuild() that area.
- Agents only steer round each other on the same navmesh. Each navmesh is for one agent size; big and small
  monsters need two navmeshes.
- There are no area costs (a swamp that is slow to cross, a forbidden path): every walkable place is the same.
