# Making a top-down game {#topdown}

A character that walks in 8 directions, enemies that chase through corridors, trees that cover the character in the right place. The sample game
`njin_topdown` (@ref samples) uses exactly these things. Never made anything before? Start with @ref first_walk : a
character that walks, in 50 lines.

@image html topdown.gif "The njin_topdown sample game: the character walks and dashes, the pink slime chases, the interact key hint shows up near a sign"

@include topdown_nav.cpp

## The character: njin::topdown_body

Attach njin::topdown_body next to a transform and a collider (box or circle). The engine moves it in
`phase_fixed_update` with collision_move(), so it slides along walls instead of sticking to them.

- Write the walking direction into `body.input.move`. A length greater than 1 is brought down to 1: **walking diagonally is no faster**
  than straight, even when you add two axes together.
- `speed`, `accel`, `decel` control whether it feels slippery or grippy.
- **Dash**: set `dash_speed` above 0 then write `input.dash = true` (a request, cleared automatically).
  `dash_time`, `dash_cooldown` adjust the length and recovery. njin::body_dashed gives you a hook for sound and dust.
- `facing` is the most recent nonzero facing direction; use it to pick the image and to swing a sword toward the right side.

njin::topdown_input_map reads two axes and one action for you.

## Y-sorting

A tree must cover a character standing behind it, and be covered by the character when they stand in front. Turn it on for a draw layer:

@code
njin::draw_set_y_sort(ctx, 5, true); // every sprite in layer 5: larger y draws later
@endcode

`y` is `transform.pos.y + sprite::sort_offset`. Put `sprite::origin` at the feet (`{0.5, 1}`) and you do not need
`sort_offset`. Tilemaps in that layer still draw before every sprite, as the background. Put **trees, houses, characters, enemies,
chests** in the same layer, and place the collider at the base of the body so collision matches where the thing stands.

## Pathfinding

njin::nav_grid is a grid of cells with a cost for entering each cell (0 is an obstacle). Build it from what already exists:

@code
g.nav = njin::nav_grid_from_world(ctx, njin::level_bounds(ctx, level), {16, 16}, layer_world);
@endcode

Every non-trigger collider in `layer_world` (including tilemap cells other than `tile_none`) becomes an
obstacle. Edit single cells when a door opens or a wall breaks: nav_set_cost(), nav_set_area().

| Task | Function |
|---|---|
| Shortest path (A\*) | nav_find_path(): diagonal moves, no corner-cutting on walls, path simplification |
| Can they see each other | collision_line_of_sight() |
| Follow a path | njin::nav_agent, nav_steer(): the direction to go, assigned to `topdown_body::input.move` |

Two things to note:

- Enemies chase the **collider center** coordinates, the same place the grid was built from, not the feet. If they follow the feet,
  the enemy hugs the wall edge and gets stuck.
- A\* costs CPU, so search again only every 0.3 to 0.5 seconds and only while the enemy is chasing; `nav_path_opts::max_nodes`
  caps a goal that cannot be reached on a big map.

An enemy should chase only when it **sees** the player (the ray is not blocked by a wall), then give up when the player
is too far away. That is all the "intelligence" a small game needs.

### A whole swarm heading for one place: flow fields

Hundreds of units heading for one goal (tower defense enemies, RTS soldiers, a swarm chasing the
player) make one A\* each too costly. njin::nav_flow works it out once for the whole grid: every cell
knows which way leads to the nearest goal, and each unit only reads its own cell.

| Task | Function |
|---|---|
| Build the flow towards one goal or several | nav_flow_build(); build again when the goal moves to another cell or the grid changes |
| Direction to move from a position, of length 1 | nav_flow_dir(): looks a few cells ahead, so units walk smooth diagonals |
| How far is left (cost as A\*), negative if unreachable | nav_flow_cost() |

@code
// Whenever the player enters another cell: once for the whole swarm.
const njin::cell now = njin::nav_cell_at(g.nav, player_pos);
if (now.x != g.player_cell.x || now.y != g.player_cell.y) {
  njin::nav_flow_build(g.flow, g.nav, player_pos, {.max_cost = 60});
  g.player_cell = now;
}
for (auto [e, tr, body] : reg.view<njin::transform, njin::topdown_body, enemy>().each())
  body.input.move = njin::nav_flow_dir(g.flow, g.nav, tr.pos);
@endcode

For several goals (every building of the player) pass the whole list: each unit heads for the one
nearest to it. `max_cost` limits the work to an area around the goal, cheaper on a big map. To keep
the swarm from piling up, push its members apart with njin::spatial_separate() (@ref collision).

## Placing things on a grid

Village builders, farms and grid inventories put things several cells big that must not overlap.
njin::place_grid remembers which thing holds each cell. Each thing is a block of `w` x `h` cells whose
top-left cell is `at`, carrying an `id` the game chooses (other than njin::place_none, e.g. the
building's index plus 1). Turning it 90 degrees swaps `w` and `h`.

| Task | Function |
|---|---|
| The cell under the mouse, snapping the dragged thing to cells (mouse in its middle) | place_cell_at(), place_snap() |
| The thing's world area, to draw it or place a sprite | place_rect() |
| Can it go there (inside the grid, no overlap) | place_fits(); `ignore` is the thing being moved |
| Put, move, take off | place_put(), place_remove() |
| Which thing is in this cell | place_owner() |
| The first free spot (picking an item into a bag, placing something just bought) | place_find_free() |

@code
njin::place_grid village = njin::place_grid_make({0, 0}, {32, 32}, 40, 40);

// Dragging a 3x3 house with the mouse: green if it fits, red if not.
const njin::cell at = njin::place_snap(village, njin::scr2w(ctx, njin::mouse_pos(ctx)), 3, 3);
const bool ok = njin::place_fits(village, at, 3, 3, dragging_id);
njin::draw_rect(ctx, njin::place_rect(village, at, 3, 3), ok ? green : red);
if (ok && njin::mouse_released(ctx, njin::mouse_left)) {
  njin::place_put(village, at, 3, 3, dragging_id); // moves it if the house is already on the grid
  njin::nav_set_area(g.nav, njin::place_rect(village, at, 3, 3)); // enemies walk around the house
}
@endcode

The placing grid does not block paths: to make enemies walk around, also mark the thing on a
njin::nav_grid as above (when moving it, free the old spot with `nav_set_area(..., 1)`). Save the grid
with the game: `owner` is an array of numbers.

## Animated tiles

Water, torches, swaying grass: declare an animation for the tile in Tiled (the tileset's animation editor) and the engine plays
them, even when the map has been baked into chunk images. Or set them yourself with tilemap_animate(). See @ref tilemap.
