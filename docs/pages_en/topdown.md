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

## Animated tiles

Water, torches, swaying grass: declare an animation for the tile in Tiled (the tileset's animation editor) and the engine plays
them, even when the map has been baked into chunk images. Or set them yourself with tilemap_animate(). See @ref tilemap.
