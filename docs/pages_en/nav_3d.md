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

## Areas and costs {#nav_3d_areas}

Every walkable place belongs to an **area**, a number from 0 to 15 (njin::nav3d_max_areas). Area 0 is plain
ground; the game gives the other numbers their meaning: road, grass, swamp, shallow water, doorway. Mark areas in
two ways:

- A whole piece of geometry: the last `area` parameter of navmesh3d_add_mesh(), navmesh3d_add_model(),
  navmesh3d_add_box(), navmesh3d_add_terrain() and navmesh3d_add_link() (links have an area too: jumping a gap
  can be expensive).
- An upright block on the map: navmesh3d_add_area() with a centre, a size, a turn about the vertical axis and the
  area number. On a navmesh already built, the tiles the block touches are rebuilt at once; navmesh3d_remove_area()
  drops the block.

A **filter** (njin::nav3d_filter) says how pathfinding sees the areas: `cost[area]` is the price of each metre (1
is normal, 8 means a metre there costs as much as eight metres of plain ground) and `excluded` holds the areas
that are forbidden outright (bit `1 << area`). Each navmesh keeps 16 filters, set with navmesh3d_set_filter();
changing a filter does **not** need a rebuild, and agents walking by it find their path again at once. Filter 0
is the default: navmesh3d_path() without a filter uses it. An agent picks its filter with
`nav3d_agent_desc::filter` and changes it with nav3d_agent_set_filter().

```cpp
constexpr njin::u8 swamp = 2, door = 3;
njin::navmesh3d_add_area(ctx, nav, {-4.0f, 0.0f, 0.0f}, {10.0f, 4.0f, 14.0f}, 0.0f, swamp);
njin::nav3d_filter f{};
f.cost[swamp] = 8.0f;     // go round the swamp, unless that is much too far
f.excluded = 1u << door;  // never through the locked door
njin::navmesh3d_set_filter(ctx, nav, 1, f);
std::vector<njin::vec3> path;
njin::navmesh3d_path(ctx, nav, from, to, path, 1); // find the path with filter 1
```

Where two surfaces lie close together (a road laid over the floor), the higher area number wins. A block from
navmesh3d_add_area() always wins over the area of the geometry under it.

## Several agent sizes

The gap kept from walls (`agent_radius`) is worked out at build time, so each agent size needs its own navmesh.
navmesh3d_clone() copies a navmesh's geometry, areas, links, obstacles and filters into a new navmesh built for
another size; build both, then add big agents to the big navmesh. Geometry, areas or obstacles added later are
added to each navmesh. Agents on two different navmeshes do not steer round each other.

## Moving obstacles

navmesh3d_add_obstacle() puts a box (or a cylinder, when `radius` is above 0) on the navmesh: where it stands is
no longer walkable, kept `agent_radius` away like a wall. navmesh3d_move_obstacle() moves it,
navmesh3d_remove_obstacle() removes it. No geometry to add and no navmesh3d_rebuild() call: the engine rebuilds
the tiles it touches over the next frames, at most `navmesh3d_desc::obstacle_tiles_per_frame` tiles a frame (4
by default), and agents find a way round it. navmesh3d_pending_tiles() tells how many tiles are still waiting.
Rebuilding an 8 m tile takes a few milliseconds, so move an obstacle when it really changes place (the crate
comes to rest, the door has closed), not every frame while it slides.

## Full example

A room with walls and a platform with a ramp; eight agents walk to where the left mouse button clicks; the path
from the first one to the mouse is drawn as a preview.

@include nav3d.cpp

Areas, filters, two agent sizes and a moving obstacle: the guard goes through the door, keeps out of the swamp and
takes the road; the big monster has its own navmesh; key K takes the guard's key away.

@include nav3d_areas.cpp

## From Lua

Scripts (@ref scripting) can call `njin.nav3d_path(navmesh, from, to)` (a table of points, or nil),
`njin.nav3d_set_target(agent, target)`, `njin.nav3d_stop`, `njin.nav3d_position`, `njin.nav3d_velocity` and
`njin.nav3d_arrived`, where `navmesh` and `agent` are the handle's `id` number passed over from C++ (for example
with script_set_global()). Areas and obstacles:

- `njin.nav3d_path(navmesh, from, to, filter)`: find the path with filter number `filter`.
- `njin.nav3d_set_filter(navmesh, index, {cost = {[2] = 8}, exclude = {3}})`, `njin.nav3d_agent_filter(agent, index)`.
- `njin.nav3d_add_area(navmesh, center, size, yaw, area)` returns the block's number; `njin.nav3d_remove_area(navmesh, id)`.
- `njin.nav3d_add_obstacle(navmesh, {position = ..., size = ..., yaw = 0, radius = 0})` returns the obstacle's
  number; `njin.nav3d_move_obstacle(navmesh, id, position, yaw)`, `njin.nav3d_remove_obstacle(navmesh, id)`.

## Limits

- An obstacle cuts the navmesh by rebuilding every tile it touches (a few milliseconds per 8 m tile), not through
  Detour's tile cache: right for crates, parked cars, doors; not for dozens of obstacles all moving every frame.
- Agents only steer round each other on the same navmesh, so big agents (big navmesh) and small ones do not.
- There are only 16 area numbers (0 to 15).
