# Procedural map generation {#procgen}

Instead of drawing every tile, you describe **how** the map is made: noise for the shape, a few rules for a natural look, or Wave
Function Collapse (WFC) for pieces that fit together. Every time you change the `seed` you get a new map, and the same `seed` always gives
the same map.

@image html procgen_topdown.gif "The top-down example running for real: every press of R is a new island, with rounded shores and rock outcrops. The camera is pulled back to show the whole map"

@image html procgen_platformer.gif "The platformer example running for real: every press of R is a new level, with pits, caves, platforms, slopes and rounded corners"

## Three steps

Everything goes through a temporary grid, njin::tile_grid, which is only data, so no window or njin::njin_ctx is needed:

| Step | What it does | Functions |
|---|---|---|
| 1. Generate | Produces the rough shape | njin::generate_topdown(), njin::generate_platformer(), njin::wfc_generate() or use njin::noise_2d() yourself |
| 2. Rules | Make it natural: remove stray dots, add borders, scatter flowers | njin::grid_majority(), njin::grid_border(), njin::grid_scatter()... |
| 3. Rounded corners | Turn "on paper" tiles into tiles with rounded corners | njin::grid_autotile() |
| Done | Put it into a map for drawing and collision | njin::tilemap_from_grid() |

The two full examples below go through all three steps: one for top-down, one for platformer. Both run in the folder of the
matching sample game (they need `assets/terrain.png`, see @ref procgen_autotile). **R** makes a new level.

@include procgen_topdown.cpp

@include procgen_platformer.cpp

## Noise: from simple to complex

njin::noise_2d() gives a number from 0 to 1 at each point, smooth across space. njin::noise_desc has every knob, and
each knob does one easily visible thing:

| Knob | Effect | When to adjust |
|---|---|---|
| `seed` | Changes the whole map | Every level |
| `frequency` | Small: big regions; large: fragmented regions (0.02 to 0.1) | First: it decides the size of regions |
| `octaves` | The number of detail layers stacked on top | You want small detail on the edges (3 to 6) |
| `gain` | The strength of the small layers | You want rugged edges (0.5 default) |
| `lacunarity` | How many times smaller each layer is than the previous one | Rarely |
| `fractal` | `fractal_fbm` terrain, `fractal_ridged` mountain ridges and rivers, `fractal_billow` rounded hills | Changing the character |
| `warp` | Bends the coordinates (measured in tiles) | You want twisting outlines (4 to 20) |

The snippet below prints noise as text (`#` is where noise is above a threshold) with each knob turned on step by step. This is real output from a real run:

@include procgen_noise.cpp

```text
1. one layer, frequency 0.06
.################.................################..........
..###############...............##############..............
....#############.............###############...............
......###########............################...............
........#########............################.........######
.........########............#################....##########
.........########.............#################.############
........#########..............##############...############
........#########................##########.......##########
##....###########..................#####............########
##################...................................#######
##....#############...................................######
........#############.................................######
..........#############..............................#######
2. fBm, 5 layers
...############..####.............#...########...#..####...#
#.##############..##.............###...#######..............
.###############.#####..........##############..............
......################........#################......####...
......#########..####.........###############.......########
...........####................##############....###########
...............................########...##...#############
..######..#.##.####...........#########......###############
..########....#####..........#######..####...####.##########
...########...........................####..........########
##.########..######....................#................####
####.......#########.....................................###
###......##############...............................######
.........###############...........................#########
3. fBm, 5 layers, gain 0.7
...###########...####..........#..#...########...#..####...#
#.##############..###............###...#######............#.
.######.########.######.........##############...........##.
......###################.#..##################......#####..
.....######.###..######.......###############.......########
............###....#............#######.####.....###########
................................######....##...#############
.#######..#.##.####..........###..####.......######.....####
.#########....##.###.......#########...###...####.##########
...#######........#...................#####.#.....####..####
#..#######...##.###...................##................##..
####.......#########.##...............###................###
####........########.##...............................######
........##########..####..............##...........###....##
4. fBm, 5 layers, warp 8
..##########..####.............#..################...#######
#################................####....########.......###.
###################.............#################...........
....################.........####################...........
....########.####............####################.......####
.....######....................################......#######
........##....................################....##########
.............#...............############..###..############
.#####.#.#######............#############.....#####.########
.#######.................................###..#....#..######
.#######...#.####........................##..............###
###.....###########..#.................................#####
##.....################..............................#######
......#################.............#######..........#######
5. ridged, 4 layers
###................###................#####.......#.########
#####..............###..............#####.............####.#
....##............###..............###................###...
.....###.........####..............##.................##....
......###........####.................................##....
........###....#.#.###...........##..................#######
.........###........###.............................########
.........##.........####...........#.................######.
........####........###.............###..............#####.#
#.......####.......###................###...........###.####
###.....####.......###.................###......######.....#
######.###..........#...................###########........#
#########.........#.#........................####...........
.########.............#.....................................
```

Read from top to bottom: (1) one layer for smooth blobs; (2) add layers and the edges get detail; (3) a high `gain` makes the edges more
rugged; (4) `warp` bends the blobs so they look natural; (5) `fractal_ridged` gives narrow ridges, good for mountains or rivers.

@note fBm noise clusters around 0.5 and rarely goes outside 0.2 to 0.8, so a "threshold of 0.3" does not mean 30% of the map.
njin::generate_topdown() stretches the values to cover 0 to 1 by itself, so the biome thresholds are **percentages** of the map. When
you use njin::noise_2d() yourself, set the threshold by eye as in the snippet above, or rank the values and take a percentile.

## Rules: making the map look natural

Raw noise is often fragmented: stray grass dots in the water, one-tile puddles, jagged edges. The rules fix exactly those things, and they run
on any njin::tile_grid, whether it comes from noise, WFC or a text map (njin::tile_grid_from_text()).

| Rule | What it does | Example |
|---|---|---|
| njin::grid_majority() | Each tile follows the majority type around it | Remove stray dots, smooth edges |
| njin::grid_smooth() | A cellular automaton for two tile types | Round caves |
| njin::grid_remove_small(), njin::grid_merge_small() | Remove small regions | One-tile ponds, stray islands |
| njin::grid_keep_largest() | Keep only the largest walkable area | No isolated corner |
| njin::grid_border() | Border one tile type around another | Sand between water and grass, grass on top of the ground |
| njin::grid_scatter() | Scatter randomly, with a minimum distance and conditions | Flowers, bushes, rocks, torches |

@include procgen_rules.cpp

The map before and after applying the three rules (`.` is water, `,` is sand, `#` is grass):

```text
Before applying the rules
,##,.,,.#,,.,,#...,,,#,..,.,...,######......,###.,,,#####,..
#####......####..,,..,,######....#######...,,#,......,,#####
###,......####....#,,###,.......#,######....,##......,,,#,.,
,,###.,#####,#.....,#####,.....,,####,......##,,,,,##.,,,.##
..####,,,#####,......####,..,.#####,#......,.,...,##,..,####
....,,...,##......#..,,####,,,####,......,##,,.....#,,,..,#,
,###,,,..,#,,,..###,...##,...###......,#####.......##...,###
#############,,,###...,#,,.,,##,.....#######,.....,##,...,#,
.###########,,,...,,###..,#####....,#########....,,,#,,....,
#######,,###.......###,.,,.,###,.....,########....,,,##,#,,,
.,###,....#####...,###,.,########......####,,,...#######,,##
,,.......,############,.....,##,.,...,,#######,.....,,##,,.,
......,,.,#,,#####.,.,..#.......,..,########,##,...........#
##########,..,,,.,,#,,,#######,#,......,,,,,....###,,#####,,
After applying the rules
####,......,##,...,######,......,####,......,.......,#######
####,......,##,...,######,......,#####,.....,........,,#####
####,.....,##,....,######,......,#####,.....,........,,,####
####,.,,,,###,.....,,####,.....,#####,............,,,,,,####
#####,######,........,###,....,###,,,.......,.....,,,,,,,###
############,.........,##,...,##,,.......,,,.......,,,...,##
###########,...........,#,..,##,......,,,##,.......,,....,##
###########,..........,#,,,,##,......,#####,.......,,....,##
###########,........,,#,,,,###,......,######,.............,#
#####,,,,,##,......,##,,,,#####,.....,#######,......,,,,,,##
,,,,,.....,##,,...,###,,.,,,###,......,######,.....,########
.........,#####,,,####,.....,,,.......,######,......,,######
.........,,#########,,................,#####,,........,#####
...........,##########,................,###,.........,######
```

The scattered grass becomes a few solid blocks, the stray dots disappear, and where grass touches water there is a sand border. The order of the rules matters:
smooth and remove small regions first, borders last.

## The top-down generator

njin::generate_topdown() takes a table of **biomes** (njin::biome) by height and, if you like, moisture:

- `biomes`: checked in order, the first biome that satisfies `max_height` and `max_moisture` is chosen. For example
  `{{water, 0.3}, {sand, 0.35}, {grass, 0.8}, {rock, 1.0}}`: the lowest 30% is water, then sand, grass, and the peaks are rock.
- `island`: pulls the height down toward the edges, so the map is an island in the sea.
- `walkable`, `blocked_tile`: keeps only the **largest** walkable area, and the rest becomes obstacles. The starting point
  (`result.spawn`) is always inside it, so the player is never locked in.
- `border_tile`: a ring of wall around the map.
- `smooth`, `min_region`: the number of smoothing passes and the smallest region size that is kept.
- Moisture (`moisture_noise`, `max_moisture`) gives deserts and forests at the same height.

`result.heights` returns the height of every tile, for your own rules to use (for example, placing trees at moderate heights).

## The platformer generator

njin::generate_platformer() builds the ground line with one-dimensional noise, then carves caves, digs pits, and places platforms. Unlike top-down,
it has **playability rules**, because a side-view level that you cannot jump across is broken:

| Rule | Parameter | Default | Why |
|---|---|---|---|
| Two adjacent columns differ by no more than `max_step` tiles | `max_step` | 2 | The default character jumps almost 3 tiles high |
| A pit is no wider than `pit_max` tiles | `pit_min`, `pit_max` | 1 to 2 | The jump reaches about 59 px, a 2-tile pit is 32 px |
| The column before a pit and the column after it are equal | | | Avoids stepping down right at the pit's edge with no room to push off |
| Both ends of the level are flat, with no pits | `safe_columns` | 6 | The start and the goal |
| Caves are not carved up to the ground surface | `cave_margin` | 4 | No holes in the floor, no falling into a cave mid-route |
| Floating platforms are lower than jump height | `platform_height` | 3 | Can be jumped onto |
| Ground that rises or falls by exactly 1 tile can be a slope | `slope_r_tile`, `slope_l_tile` | unused | A smoother climb |

`caves` is the **fraction of the underground area** that gets carved (0.2 is about 20%): caves are picked by the rank of the noise, then rounded
with a cellular automaton, and pockets under 8 tiles are filled in.

@note The numbers above are for the default njin::platformer_body (runs at 110 px/s, jumps about 45 px high, about
59 px far). If you change the character's `run_speed` or `jump_speed`, change `pit_max`, `max_step` and `platform_height` to match.

**How it was verified:** a bot that runs right and jumps when it meets a pit or a wall, driving the real njin::platformer_body
on the very level the example above generates (160 x 30, `caves` 0.2, `pit_chance` 0.06, `platform_chance` 0.05, no slopes),
reached the goal on **all 20 consecutive seeds** (1 to 20). That is evidence for the parameters above, not a promise for every
parameter: widen `pit_max` or `max_step` beyond jump reach and the level may become impossible to cross.

## Natural rounded corners: autotile {#procgen_autotile}

A square grid looks "jagged" because every tile is a square. **Autotile** fixes that: ground is drawn with a **tile set**,
and each tile in the grid chooses which piece of the set to use depending on the tiles around it. Where the ground opens up there is an edge, where two edges meet
there is a rounded corner, where the ground curves inward there is an inner corner. You write the rule: "which tiles are ground, which tile set draws them".

njin::grid_autotile() does this on a njin::tile_grid, using a njin::autotile_rule:

| Field | Meaning |
|---|---|
| `tiles` | The grid tiles this rule replaces with tiles from the set. Grass and dirt can be the same terrain |
| `joins` | Other tiles that still count as connected, but are not replaced (rock under ground, a door in a wall) |
| `base` | The index of the first tile of the set in the tileset; the set is consecutive tiles |
| `layout` | njin::autotile_blob (47 tiles, with both convex and concave corners) or njin::autotile_edges (16 tiles, looking only at the four sides) |
| `outside` | The sides on which **outside the grid** counts as ground continuing on (bits of njin::grid_side). All four by default |

A tile looks at its eight neighbors, summed into a mask (njin::autotile_neighbor). A diagonal only counts when **both** tiles adjacent to it
along its sides are also connected (otherwise the edge already covers the corner), which leaves exactly 47 cases. njin::autotile_index() turns the mask into the tile index
in the set:

@include procgen_autotile.cpp

```text
29 ground tiles replaced by tiles of the set

  .   .   .   .   .   .   .   .   .   . 
  .   .  10  31  31  26   .   .   .   . 
  .  10  33  46  46  45  26   .   .   . 
  .   4  41  46  46  46  42   .   .   . 
  .   .   4  36  41  46  42   .   .   . 
  .   .   .   .  12  46  45  26   .   . 
  .   .   .   .   4  36  36  34   .   . 
  .   .   .   .   .   .   .   .   .   . 

tile (2, 1): mask 28 -> set tile 10 (match: yes)
```

Number 46 is the tile in the middle of a region (all eight neighbors present). The top-left corner of the block is tile 10, exactly equal to the mask
`neighbor_right | neighbor_down | neighbor_down_right` worked out by hand on the last line.

The sample game comes with two 47-tile sets (grass-topped ground and rock) in `terrain.png`, laid out in exactly the order of njin::autotile_index():

@image html procgen_autotile_sheet.png "terrain.png of the platformer sample game, drawn on a sky background: the grass-topped ground set (left) and the rock set (right), 47 tiles each, numbered in set order. Tile 46 is the tile in the middle of a region"

The same level, one side using only the "full" tile (46 and 93) for every tile, the other going through njin::grid_autotile():

@image html procgen_autotile_compare.png "Left: every tile is a full tile, no autotile. Right: the same level through njin::grid_autotile(): edges have borders, grass only on top, convex corners are rounded"

A few things worth knowing:

- **Call it last.** Autotile turns "on paper" tiles into tile-set indices, so rules that read tile types (njin::grid_border(),
  njin::grid_scatter() with a condition) must run **before**. Rules within a single call all read from a snapshot of the grid,
  so their order does not matter, and new tile indices are not confused with old tiles.
- **Collision is still square.** The rounded corners are only drawing; the tile's collision box is still full. In top-down that is usually what you want;
  in a platformer, standing right at the edge of a rounded corner will look like you are "floating" by a few pixels. If you need the shape to match exactly, use slopes (njin::tile_slope_r).
- **Multiple layers.** The top-down example above splits into three tilemaps (water, ground, and rock with flowers) using njin::tilemap::layer: the ground has rounded
  corners that reveal the water beneath, the rock has rounded corners that reveal the grass beneath. That is how you get rounded coastlines and rock outcrops without needing a separate "sand
  turning into water" tile for every pair.
- **Outside the grid.** By default the outside of the grid is ground continuing on, so the edge of the map is not rounded. Remove `side_up` from `outside` for the platformer ground,
  as in the example; set `outside = 0` for the island, as in the top-down example.
- **Drawing your own tile set.** The 47-tile set is a common convention (also called a "blob tileset"). If you use your own image, arrange the tiles in exactly the order
  of njin::autotile_index(); the script `src/games/shared/tools/make_terrain.py` is an example of how to build them from masks.

## Wave Function Collapse

WFC fills the grid so that every pair of adjacent tiles is **valid**. You do not set the shape, you set adjacency rules: "which tile may stand to the
right of, below... which tile". There are two ways to get rules:

### Learning from a sample

njin::wfc_learn() reads a sample njin::tile_grid (usually written with njin::tile_grid_from_text()): every pair of adjacent tiles
in the sample becomes an adjacency rule, and how often it occurs becomes its frequency. A small, varied sample gives good results; a pair that the sample does not contain
never appears. njin::wfc_desc::allowed pins the places you want fixed (sky in the top rows, rock in the bottom rows):

@include procgen_wfc.cpp

```text
                                                            
        ggggg          g              gg  gg g  ggg gggg ggg
       gdddddggg  gggg g       g g g  gg gdggg  gddgddddgddg
gg    ggdddsddddggdddg ggg g  gdgg gggddgddgddgggddddddddddd
ddg  gdddddsddddddddddgdgdgg ggddggddgdddddgdddddddddddddddd
dddggddddddsdddddddddddddsdg gddddddddsddddddddddddddddddddd
ddddgddddddsdddddddddddddsdgggddddddsdsdddddddddddddsdddddds
dddddddddddsdddddddddddddsdgddddddddsdsdddsdddddddddsdddddds
dddddddddddsdsdddddddddddsddddddddddsdsdddsddddddsddsdddddds
dddddddddddsdsdddddddddddsddddddddddsdsdsdsddddddsddsdddddds
dddddddddddsdsdddddddddddsddddddddddsssdsssdddddssddsdddddds
dddddddddddsdsdsdddddddddsddddddddsdsssdsssdddsdssddsdddddds
ddsdddsddddsdsdsddsddddddsddddddddsdsssdsssdsdsdssddsdddddds
ddsdddsddddsdsdsdssddddddsddddddddsdsssdsssdsdsdssddsdddddds
ddsdsdsddddsdsdsdsssdsdddsddddddsdsssssdsssdsdsdssddsdsdddds
ddsdsdsddddsdsdsdsssdsdsdsdsddsdsdsssssdsssdsdsdssddsdsdddds
sdsdsdsdsddsdsdsdsssdsdsssssddsdsssssssdsssdsssdssddsssdddds
sdsdsdsdssdsdsdsssssdsssssssdssdsssssssdsssdsssdssddsssssdds
sssdsdsdssdsdsssssssdsssssssdssdsssssssdssssssssssdsssssssds
ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss
```

Every column has the right order of sky, grass, dirt, rock, and there is no floating grass, because the sample never has grass below dirt. But the
rules work on **tile pairs**, not on patches of tiles: this WFC does not know "how wide a hill is", so the surface can be ragged and in
places turn into columns. For a better shape, use noise (the generators above) and let WFC handle the details, or give the sample more
flat stretches (grass beside grass occurs more often, so the surface tends to be flatter), or use hand-written rules as below.

### Hand-written rules: connected roads

For a tile set with **matching edges** (roads, rivers, corridors), rules are easier to write by hand than to learn from a sample: two adjacent tiles are valid when both have
a road or both have none on the side where they touch. The sixteen road tiles are numbered exactly like njin::autotile_edges (bit 1 up, 2 right, 4 down,
8 left), so the same tile set can be used to draw them too:

@include procgen_wfc_roads.cpp

```text
     ┌┐     ┌┬─┐ ┌─┐┌┐  ┌─┐         ┌─┐ 
 ┌───┼┤    ┌┘└─┘ │ │└┘  │ │         ├┐├╴
 │   ││  ┌─┼─┐ ┌─┴┐│    └─┤ ┌┐      ├┴┴┐
 ├───┘│  │╷└┐│ └┐┌┘└┐┌────┤ │└┐     └──┘
┌┘    │ ┌┘└─┘│┌─┴┘ ┌┘│ ┌─┐│ │ │   ┌─┐   
└─────┘ │ ┌──┴┘ ┌┐ ├┬┘┌┴┬┘└─┘ │┌┐┌┘ │┌─┐
   ┌───┐│ │┌────┴┘┌┘└┐└─┤┌─┐  ││││  └┘╶┘
┌┐ │┌──┘└─┼┘ ┌┐   └┐ │  ││ └─┐││└┤      
└┤┌┴┘┌──┐┌┘╷ │└┐   └┬┘ ┌┘├┬─┐│└┘┌┘┌┐ ┌─┐
 │└┬─┤ ╷└┘ └┐│ └┐   │┌┐└─┘└┬┼┘  └─┘│ └─┘
 │╶┘ └─┘    │└──┘┌┐ └┴┼─┐  ││      │┌┐  
 └┐      ┌──┘    └┴┐  └─┘  └┘      │└┤  
  └──────┘    ┌┐   └┐   ┌───┐┌┐  ┌┐└┐│  
              └┴────┘   └───┘└┘  └┘ └┘  
```

Everywhere two tiles touch, the roads match, and no road runs off the map (every pair of tiles in the result above was checked:
0 mismatches). The frequency (`weight`) decides the scene: lots of grass, straight roads are common, crossroads are rare.

### Parameters

| Parameter | Meaning |
|---|---|
| `seed` | The same seed gives the same result |
| `attempts` | WFC does not backtrack: on a dead end (a tile with no choices left) it starts over with a different random order, up to this many times. Default 20 |
| `periodic` | `true`: the right edge joins the left edge, the bottom edge joins the top edge (for tiling a background, repeatable) |
| `allowed` | A fixed constraint: whether tile `tile` may be placed at `(x, y)` |
| njin::wfc_allow(), njin::wfc_add_tile() | Build rules and change frequencies by hand |

njin::wfc_generate() returns `false` when the rules are too tight, the constraints contradict each other, or the attempts run out, and it **does not change** the output
grid. Always check the return value, and have a fallback (a hand-drawn map, or try another seed).

The result of WFC is also a njin::tile_grid, so the rules above (njin::grid_scatter(), njin::grid_autotile()...) can keep running
on it.

## Putting it into the game

njin::tilemap_from_grid() puts the grid into a njin::tilemap, with its top-left corner at tile `origin`. A tile of -1 in the grid does **not** erase the tile already
there, so you can stack several grids; to regenerate from scratch (like pressing R) call njin::tilemap_clear() first, as both examples do.
Collision and animation of tiles are decided by njin::tilemap_set_shape() and njin::tilemap_animate() like any tilemap (see
@ref tilemap). `result.spawn` (and `result.goal` for the platformer) is the tile to place the character on.

## Notes

- **The same seed gives the same map**, on every machine tried: a hash string of 12 seeds for all three generators gives the **same value**
  when built with gcc on Windows (MinGW) and on Linux (gcc 15), at `-O0`, `-O2` and `-O3 -march=native`. Not tried
  with MSVC or clang. Store the `seed` (a small number) instead of storing the whole map.
- Generation runs once when loading a level, not every frame. Measured on an `-O2` build: a 64 x 48 top-down map takes under 1 ms, a 160 x 30
  platformer level under 0.3 ms; WFC is the heaviest part (a 96 x 64 grid takes about 0.2 seconds), so generate before
  entering the level.
- **Look at it after generating.** Noise and rules give a *valid* map, not a *good* map. Change the seed a few dozen times and look.

@see njin::tile_grid, njin::generate_topdown(), njin::generate_platformer(), njin::grid_autotile(), njin::wfc_generate()
@see tilemap, level, platformer, topdown
