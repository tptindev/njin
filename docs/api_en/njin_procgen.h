#pragma once
#include "_tilemap.h"
#include "_types.h"
#include <functional>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace njin {
/// @addtogroup grp_procgen
/// @{

// ---------------------------------------------------------------------------
// Temporary tile grid for map generation
// ---------------------------------------------------------------------------

/// A fixed-size rectangular grid of tiles, for generating and editing maps before putting them into
/// njin::tilemap. Each cell holds a tile index in the tileset, or -1 for empty.
///
/// The generators (generate_topdown(), generate_platformer(), wfc_generate()) and the rules
/// (grid_majority(), grid_border()...) all work on this grid; when done,
/// tilemap_from_grid() places it into a tilemap. The grid is just data: it needs no window and no
/// njin::context, so it can be generated anywhere, even before the window is opened.
struct tile_grid {
  i32 width = 0;           ///< Number of columns.
  i32 height = 0;          ///< Number of rows.
  std::vector<i32> cells{}; ///< Cells by row, from top to bottom. `cells[y * width + x]`.

  /// Whether a cell is inside the grid. @param x Column. @param y Row. @return `true` if inside.
  bool inside(i32 x, i32 y) const { return x >= 0 && y >= 0 && x < width && y < height; }
  /// Value of a cell. @param x Column. @param y Row. @return Tile index, -1 if empty or outside the grid.
  i32 get(i32 x, i32 y) const { return inside(x, y) ? cells[(usize)(y * width + x)] : -1; }
  /// Set a cell; ignored if outside the grid. @param x Column. @param y Row. @param tile Tile index, -1 is empty.
  void set(i32 x, i32 y, i32 tile) {
    if (inside(x, y))
      cells[(usize)(y * width + x)] = tile;
  }
};

/// Create a `width` x `height` grid, every cell set to `fill`.
/// @param width Number of columns. @param height Number of rows. @param fill Initial value, -1 (empty) by default.
/// @return The new grid. A negative size is treated as 0.
tile_grid tile_grid_make(i32 width, i32 height, i32 fill = -1);

/// Build a grid from a text map, with the same rules as tilemap_from_text(): one character per cell, `legend` says
/// which character is which tile. A character not in `legend` becomes -1. The grid is as wide as the longest line.
/// Use it to write a **sample** for wfc_learn(), or to hand-edit a grid and then let the rules continue.
/// @param text Text map. @param legend Character legend.
/// @return The grid.
tile_grid tile_grid_from_text(std::string_view text, std::initializer_list<tile_key> legend);

/// Place the grid into a tilemap, with its top-left corner at tile `origin`. A -1 cell of the grid does **not** clear an existing tile (like
/// a text map), so several grids can be stacked; to regenerate from scratch call tilemap_clear() first.
/// @param map Tilemap. @param grid Grid. @param origin Tile of the top-left corner.
void tilemap_from_grid(tilemap &map, const tile_grid &grid, cell origin = {});

/// Number of cells with value `tile`. @param grid Grid. @param tile Tile index (may be -1).
/// @return Number of cells.
i32 grid_count(const tile_grid &grid, i32 tile);

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------

/// Basic noise type.
enum noise_type {
  noise_perlin, ///< Gradient noise (Perlin): smooth, rounded. Default, good for terrain.
  noise_value,  ///< Value noise: cheaper, a bit blocky. Good for stains and speckles.
};

/// How several layers of noise (octaves) are stacked on top of each other.
enum noise_fractal {
  fractal_none,   ///< One layer. Large, smooth shapes.
  fractal_fbm,    ///< Adds layers that get smaller and fainter: natural terrain. Default.
  fractal_ridged, ///< Sharp ridges: mountain ranges, rock spines, rivers when taking the low areas.
  fractal_billow, ///< Rounded, puffy ripples: clouds, low hills, pebbles.
};

/// Parameters of a noise source. The same parameters and the same `seed` give the same result, on every machine.
///
/// From simple to complex: setting only `seed` and `frequency` is enough to use it; add `octaves`, `gain`,
/// `lacunarity` for fine detail; change `fractal` to change the shape; set `warp` so contours are naturally
/// twisted instead of evenly round.
struct noise_desc {
  u32 seed = 1;                          ///< Seed. Change this number to get a different map.
  noise_type type = noise_perlin;        ///< Noise type.
  f32 frequency = 0.05f;                 ///< Frequency per tile. Small: large regions; large: fragmented regions. 0.02 to 0.1 is typical.
  noise_fractal fractal = fractal_fbm;   ///< How layers are stacked.
  i32 octaves = 4;                       ///< Number of layers (1 to 8). More layers: more detailed edges, more expensive.
  f32 lacunarity = 2.0f;                 ///< Each following layer has its frequency multiplied by this number.
  f32 gain = 0.5f;                       ///< Each following layer has its strength multiplied by this number. Large: rougher.
  f32 warp = 0.0f;                       ///< Bends the coordinates, in tiles. 0 is off, 4 to 20 gives natural contours.
  f32 warp_frequency = 0.03f;            ///< Frequency of the noise used for warping.
};

/// Noise value at a point, from 0 to 1.
/// @param desc Parameters. @param x Coordinate, usually the tile column. @param y Coordinate, usually the tile row.
/// @return A number in `[0, 1]`. With `fractal_fbm` values cluster around 0.5 and rarely fall outside 0.2 to 0.8;
/// generate_topdown() stretches them to fill 0 to 1 (see njin::topdown_gen_desc::normalize).
f32 noise_2d(const noise_desc &desc, f32 x, f32 y);

/// One-dimensional noise, from 0 to 1: the ground line of a platformer, height by column.
/// @param desc Parameters. @param x Coordinate. @return A number in `[0, 1]`.
f32 noise_1d(const noise_desc &desc, f32 x);

// ---------------------------------------------------------------------------
// Rules: make the map look natural
// ---------------------------------------------------------------------------

/// Sides of a tile, can be combined: `side_up | side_down`.
enum grid_side : u8 {
  side_up = 1,    ///< The tile above.
  side_down = 2,  ///< The tile below.
  side_left = 4,  ///< The tile to the left.
  side_right = 8, ///< The tile to the right.
  side_all = 15,  ///< All four sides.
};

/// Smoothing: each cell becomes the tile type that appears most often in the 3 x 3 area around it, if that type has at least
/// `threshold` cells. Removes stray dots and jagged edges left by noise, for all tile types at once.
/// @param grid Grid. @param iterations Number of passes. @param threshold Out of 9 cells, how many are needed (5 is a majority).
void grid_majority(tile_grid &grid, i32 iterations = 1, i32 threshold = 5);

/// Cave-style smoothing of two tile types (cellular automaton): a cell becomes `solid` if at least `threshold`
/// of the 8 cells around it are solid, otherwise it becomes `open`. A solid cell is any cell other than `open`, including outside the grid, so
/// the cave does not break through the edge. Only changes cells that are `solid` or `open`.
/// @param grid Grid. @param solid Solid tile (wall). @param open Open tile (cave interior, usually -1).
/// @param iterations Number of passes, 2 to 5 is typical. @param threshold 5 is the classic cave rule.
void grid_smooth(tile_grid &grid, i32 solid, i32 open, i32 iterations = 3, i32 threshold = 5);

/// Remove small regions of a tile type: each connected region (4-directional) of `tile` with fewer than
/// `min_size` cells becomes `replace`. Gets rid of tiny ponds, one-tile islands, stray rock patches.
/// @param grid Grid. @param tile Tile type. @param min_size Minimum size. @param replace Tile to put in.
/// @return Number of regions removed.
i32 grid_remove_small(tile_grid &grid, i32 tile, i32 min_size, i32 replace);

/// Like grid_remove_small() for **every** tile type: a region smaller than `min_size` merges into the tile type that surrounds it
/// the most. @param grid Grid. @param min_size Minimum size. @return Number of regions merged.
i32 grid_merge_small(tile_grid &grid, i32 min_size);

/// Keep all walkable places in one piece: among the cells in `walkable`, only the largest connected region (4-directional)
/// is kept, other regions become `replace`. Used for top-down so that no corner is isolated.
/// @param grid Grid. @param walkable Walkable tile types. @param replace Tile for the discarded regions.
/// @return Number of cells replaced.
i32 grid_keep_largest(tile_grid &grid, std::initializer_list<i32> walkable, i32 replace);

/// Border: a `tile` cell within `distance` cells of `touching` (along the sides in `sides`) becomes
/// `replace`. A sand beach between water and grass, grass on top of ground (only the top side touching an empty cell), rocky rims.
/// @param grid Grid. @param tile Tile to change. @param touching Tile that causes the border. @param replace Border tile.
/// @param sides Sides to consider. @param distance Border thickness, in tiles.
/// @return Number of cells changed.
i32 grid_border(tile_grid &grid, i32 tile, i32 touching, i32 replace, u8 sides = side_all, i32 distance = 1);

/// Random scatter: each `on` cell has probability `chance` of becoming `place`, and two scattered cells are at least
/// `min_distance` cells apart (0 is no limit). Flowers on grass, rocks on dirt, trees in a forest.
/// @param grid Grid. @param on Tile to scatter onto. @param place Tile to scatter. @param chance Probability, 0 to 1.
/// @param seed Seed. @param min_distance Minimum distance (Chebyshev).
/// @return Number of cells scattered.
i32 grid_scatter(tile_grid &grid, i32 on, i32 place, f32 chance, u32 seed, i32 min_distance = 0);

/// Like the version above, but where to scatter is decided by the `where` function: bushes only on empty cells **right above** the
/// ground, torches only on walls...
/// @code
/// njin::grid_scatter(grid, 14, 0.2f, 7, [](const njin::tile_grid &g, njin::i32 x, njin::i32 y) {
///   return g.get(x, y) == -1 && g.get(x, y + 1) == 0; // empty, and right below is grass
/// });
/// @endcode
/// @param grid Grid. @param place Tile to scatter. @param chance Probability. @param seed Seed.
/// @param where Whether cell `(x, y)` may be scattered onto. @param min_distance Minimum distance.
/// @return Number of cells scattered.
i32 grid_scatter(tile_grid &grid, i32 place, f32 chance, u32 seed,
                 const std::function<bool(const tile_grid &, i32, i32)> &where, i32 min_distance = 0);

// ---------------------------------------------------------------------------
// Natural rounded corners: autotile
// ---------------------------------------------------------------------------

/// Tile set used to join a terrain type with itself.
enum autotile_layout {
  /// 16 tiles, looking at the four adjacent tiles (up, right, down, left). **Convex** corners (two adjacent sides left open) can be rounded;
  /// **concave** corners (open diagonal tile) cannot. Enough for walls and paths.
  autotile_edges,
  /// 47 tiles, looking at all eight adjacent tiles. Has both convex and concave corners: ground, water, rock faces join naturally. Default.
  autotile_blob,
};

/// Adjacent tiles, combined into the mask of a tile. A diagonal tile only counts when **both** tiles orthogonally adjacent to it are also
/// of the same type, so only 47 mask patterns remain (not 256).
enum autotile_neighbor : u8 {
  neighbor_up = 1,          ///< The tile above.
  neighbor_up_right = 2,    ///< The diagonal tile to the upper right.
  neighbor_right = 4,       ///< The tile to the right.
  neighbor_down_right = 8,  ///< The diagonal tile to the lower right.
  neighbor_down = 16,       ///< The tile below.
  neighbor_down_left = 32,  ///< The diagonal tile to the lower left.
  neighbor_left = 64,       ///< The tile to the left.
  neighbor_up_left = 128,   ///< The diagonal tile to the upper left.
};

/// Number of tiles in a set. @param layout Set type. @return 16 or 47.
i32 autotile_count(autotile_layout layout);

/// Tile index (in the set, from 0) for a mask. With `autotile_edges`: `up + 2 * right + 4 * down +
/// 8 * left`, each being 1 if that adjacent tile is of the same type (diagonals are ignored); a fully isolated tile is 0, a tile in the middle
/// of a region is 15. With `autotile_blob`: the rank of the mask (with redundant diagonals removed) among the 47 valid masks sorted in
/// ascending order; an isolated tile is 0, a tile in the middle of a region is 46. Draw the tile set in exactly this order (the Map generation page has a table).
/// @param mask The `neighbor_*` bits of the adjacent tiles of the same type. @param layout Set type.
/// @return Index in `[0, autotile_count())`.
i32 autotile_index(u8 mask, autotile_layout layout = autotile_blob);

/// A rounding rule: which tiles are the same terrain and which tile set draws them.
struct autotile_rule {
  /// The grid tiles that this rule replaces with tiles from the set (for example grass and dirt are both "ground").
  std::vector<i32> tiles{};
  /// Other tiles still counted as joined (no edge is created), but not replaced: rock under dirt, a door in a wall.
  std::vector<i32> joins{};
  i32 base = 0;                        ///< Tile index of the first tile of the set in the tileset. The set is consecutive tiles.
  autotile_layout layout = autotile_blob; ///< Set type.
  /// The sides on which **outside the grid** counts as the same type (the njin::grid_side bits). All four by default: the map edge
  /// is not rounded, the ground continues outward. Remove `side_up` if the ground must have an edge on top.
  u8 outside = side_all;
};

/// Replace every tile in `rules[i].tiles` with a tile from the corresponding set, chosen by the adjacent tiles: where the terrain opens up
/// the tile has an edge, where two edges meet there is a rounded (convex) corner, where the terrain is indented there is a concave corner. All rules read
/// from **the same snapshot** of the grid before changing, so the order of the rules does not matter and the new tile indices
/// are not confused with old tiles. A tile matching several rules goes to the first rule.
///
/// Call it **last**, after every other rule (grid_border(), grid_scatter()...): because tiles have already turned into set tiles.
/// @code
/// // grass and dirt are one terrain, rock joins with it; outside the grid counts as sky on top
/// njin::grid_autotile(grid, {{.tiles = {grass, dirt}, .joins = {stone}, .base = 0, .outside = njin::side_all & ~njin::side_up},
///                            {.tiles = {stone}, .joins = {grass, dirt}, .base = 47}});
/// @endcode
/// @param grid Grid. @param rules Rules.
/// @return Number of cells replaced.
i32 grid_autotile(tile_grid &grid, const std::vector<autotile_rule> &rules);

// ---------------------------------------------------------------------------
// Top-down: terrain by height and moisture
// ---------------------------------------------------------------------------

/// A terrain region of a top-down map: which tile is placed where the height and moisture are what. Considered in order, the first
/// region satisfying `height <= max_height` and `moisture <= max_moisture` is chosen.
struct biome {
  i32 tile;                 ///< Tile placed for this region.
  f32 max_height = 1.0f;    ///< Maximum height (0 to 1).
  f32 max_moisture = 1.0f;  ///< Maximum moisture (0 to 1). Leave at 1 if moisture is not used.
};

/// Parameters for generating a top-down map.
///
/// Default (only `biomes` needed): a height from noise, split into regions by thresholds, smoothed once, regions
/// under 6 tiles removed. Add more gradually: moisture for deserts and forests at the same height, `island` for an island shape, `walkable`
/// so that all walkable places form one piece, `border_tile` for an outer wall.
struct topdown_gen_desc {
  i32 width = 64;                 ///< Number of columns.
  i32 height = 64;                ///< Number of rows.
  noise_desc height_noise{};      ///< Height noise.
  /// Moisture noise. Only used when a region sets `max_moisture` below 1.
  noise_desc moisture_noise{.seed = 7919, .frequency = 0.03f, .octaves = 3};
  std::vector<biome> biomes{};    ///< The regions, considered in order. A tile matching no region uses the last region.
  f32 island = 0.0f;              ///< 0 is off. 0.5 to 1: height falls off toward the edge, making an island in the water.
  /// Stretches height (and moisture) so the lowest tile of the map is 0 and the highest is 1. When on (default), the
  /// thresholds of `biomes` read as **percentages of the map**: `max_height = 0.3` is about the lowest 30%.
  /// When off, thresholds compare directly with the noise, and fBm noise rarely falls outside the range 0.2 to 0.8.
  bool normalize = true;
  i32 smooth = 1;                 ///< Number of grid_majority() passes. 0 is off.
  i32 min_region = 6;             ///< grid_merge_small(): regions smaller than this many tiles merge into their surroundings. 0 is off.
  /// Walkable tiles. If set, only the largest connected walkable region is kept (grid_keep_largest()), and the spawn
  /// point lies inside that region.
  std::vector<i32> walkable{};
  i32 blocked_tile = -1;          ///< Tile replacing a discarded walkable region, when `walkable` is set.
  i32 border_tile = -1;           ///< Tile for the ring of wall around the map. -1 means none.
};

/// Result of generate_topdown().
struct topdown_gen_result {
  tile_grid grid{};               ///< The map.
  std::vector<f32> heights{};     ///< Height (0 to 1) of each tile, in the same order as `grid.cells`. For custom rules.
  cell spawn{};                   ///< Spawn point: the walkable tile closest to the center (the map center if there is no `walkable`).
};

/// Generate a top-down map from noise. @param desc Parameters. @return The map, heights and spawn point.
topdown_gen_result generate_topdown(const topdown_gen_desc &desc);

// ---------------------------------------------------------------------------
// Platformer: ground, caves, pits, platforms
// ---------------------------------------------------------------------------

/// Parameters for generating a platformer level, side view, going from left to right.
///
/// The ground is a noise line by column. The rules keep the level **playable**: two adjacent columns differ by no
/// more than `max_step` tiles (set to no more than the character's jump height), caves are not carved up to the ground surface,
/// pits are no wider than `pit_max` tiles, and both ends of the level are always flat.
struct platformer_gen_desc {
  i32 width = 120;                  ///< Number of columns.
  i32 height = 30;                  ///< Number of rows.
  /// Ground noise, by column.
  noise_desc surface_noise{.seed = 1, .frequency = 0.04f, .octaves = 3};
  i32 ground_min = 12;              ///< Highest row the ground can rise to (rows counted from the top).
  i32 ground_max = 24;              ///< Lowest row the ground can drop to.
  i32 max_step = 2;                 ///< Maximum difference between two adjacent columns, in tiles.
  i32 surface_tile = 0;             ///< Ground surface tile (grass).
  i32 dirt_tile = 1;                ///< Tile under the ground surface.
  i32 deep_tile = -1;               ///< Deep tile (rock). -1 means use `dirt_tile`.
  i32 deep_depth = 6;               ///< From this depth (tiles counted from the ground surface) downward is `deep_tile`.
  /// Slope tiles high on the right and high on the left (njin::tile_slope_r, njin::tile_slope_l), placed where the ground
  /// rises or falls by exactly one tile. -1 means no slopes. Remember to set their shape with tilemap_set_shape().
  i32 slope_r_tile = -1;
  i32 slope_l_tile = -1;            ///< See `slope_r_tile`.
  /// Cave noise.
  noise_desc cave_noise{.seed = 3, .frequency = 0.09f, .octaves = 2};
  /// Fraction of the underground area carved into caves, 0 (off) to 0.9. It is a **real** fraction (taken by the rank of the noise
  /// over exactly the allowed tiles): 0.2 carves about 20% of the tiles below `cave_margin`, whatever range of values the noise has. Afterwards the caves are
  /// rounded with two cellular automaton passes and pockets under 8 tiles are removed, so the actual number differs
  /// a little. 0.15 to 0.3 is moderate; more than that is a maze of tunnels.
  f32 caves = 0.0f;
  i32 cave_margin = 4;              ///< Number of rows right below the ground surface that are not carved into caves.
  f32 pit_chance = 0.0f;            ///< Probability of starting a pit at each column. 0 means no pits.
  i32 pit_min = 1;                  ///< Narrowest pit, in tiles.
  /// Widest pit. Keep it within the character's long-jump reach: the default njin::platformer_body runs at 110 px/s and
  /// stays airborne about 0.54 seconds, roughly 59 px, so a 2-tile pit (32 px) is comfortable; a 3-tile pit (48 px, plus the character's width)
  /// requires taking off right at the edge. Raise the character's `run_speed` or `jump_speed` before raising this number.
  i32 pit_max = 2;
  i32 safe_columns = 6;             ///< Number of flat, pit-free columns at each end of the level (spawn and goal).
  f32 platform_chance = 0.0f;       ///< Probability of placing a floating platform at each column. 0 means no platforms.
  i32 platform_tile = -1;           ///< Tile of the platform (usually a one-way platform, njin::tile_one_way).
  i32 platform_min = 3;             ///< Shortest platform.
  i32 platform_max = 5;             ///< Longest platform.
  i32 platform_height = 3;          ///< Platforms are this many tiles above the ground. Keep it within the jump height.
};

/// Result of generate_platformer().
struct platformer_gen_result {
  tile_grid grid{};                 ///< The level.
  std::vector<i32> surface{};       ///< Row of the ground at each column, -1 at a column that is a pit.
  cell spawn{};                     ///< Empty tile right above the ground, at the left end: where to place the character.
  cell goal{};                      ///< Empty tile right above the ground, at the right end: where to place the goal.
};

/// Generate a platformer level from noise. @param desc Parameters. @return The level, ground, spawn point and goal.
platformer_gen_result generate_platformer(const platformer_gen_desc &desc);

// ---------------------------------------------------------------------------
// Wave Function Collapse
// ---------------------------------------------------------------------------

/// Direction from a tile to its neighbor, used for the WFC adjacency rules.
enum wfc_dir { wfc_right, wfc_down, wfc_left, wfc_up };

/// Rules for Wave Function Collapse: which tiles exist, how common each tile is, and which tile may stand next to which
/// tile in each direction. Build with wfc_learn() from a sample, or by hand with wfc_add_tile() and wfc_allow().
struct wfc_rules {
  std::vector<i32> tiles{};             ///< The tiles (index in the tileset; -1 is the empty tile, which is also a tile).
  std::vector<f32> weights{};           ///< How common each tile is. Large: appears more.
  /// Adjacency rules, as bits: `allow[(i * 4 + dir) * words + j / 64]` has bit `j % 64` set if tile number `j` may stand
  /// in direction `dir` of tile number `i`. Use wfc_allow() instead of editing directly.
  std::vector<u64> allow{};
  i32 words = 0;                        ///< Number of 64-bit words per tile set.
};

/// Add a tile to the rules (or change its frequency if it already exists). @param rules Rules. @param tile Tile.
/// @param weight How common it is. @return Index of the tile in `rules.tiles`.
i32 wfc_add_tile(wfc_rules &rules, i32 tile, f32 weight = 1.0f);

/// Allow `b` to stand in direction `dir` of `a`, and (symmetrically) `a` in the opposite direction of `b`. A tile that does not exist yet
/// is added with frequency 1.
/// @param rules Rules. @param a First tile. @param dir Direction from `a` to `b`. @param b Second tile.
void wfc_allow(wfc_rules &rules, i32 a, wfc_dir dir, i32 b);

/// Learn rules from a sample: every pair of adjacent tiles in the sample becomes an adjacency rule, the number of occurrences becomes the frequency.
/// Write the sample with tile_grid_from_text(). A small, varied sample gives good results; any pair the sample lacks
/// never appears.
/// @param sample Sample. @param periodic `true`: treat the sample as repeating, the right edge joins the left edge, the bottom edge joins the top edge.
/// @return The rules.
wfc_rules wfc_learn(const tile_grid &sample, bool periodic = false);

/// Parameters of wfc_generate().
struct wfc_desc {
  i32 width = 32;         ///< Number of columns.
  i32 height = 32;        ///< Number of rows.
  u32 seed = 1;           ///< Seed.
  i32 attempts = 20;      ///< Number of retries on running into a dead end (contradiction), each with a new random sequence.
  bool periodic = false;  ///< Tileable result: the right edge matches the left edge, the bottom edge matches the top edge.
  /// Constraint: whether tile `tile` may be placed at `(x, y)`. Leave empty to allow everywhere. Use it to fix
  /// the bottom row as ground, the top row as sky, the outer ring as wall...
  std::function<bool(i32 x, i32 y, i32 tile)> allowed{};
};

/// Generate a grid satisfying every adjacency rule, using Wave Function Collapse (tile model, no backtracking: on a contradiction
/// it starts over with a different random sequence, at most `attempts` times).
/// @param rules Rules. @param desc Parameters. @param out Receives the grid on success; unchanged on failure.
/// @return `true` on success. `false` when the rules are too tight, the constraints contradict, or attempts run out.
bool wfc_generate(const wfc_rules &rules, const wfc_desc &desc, tile_grid &out);

/// @}
} // namespace njin
