#pragma once
#include "_math.h"
#include "_tilemap.h"
#include "_types.h"
#include <utility>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_nav
/// @{

/// Pathfinding grid: each cell has a cost to enter, 0 means impassable.
///
/// Usually built once when a level loads with nav_grid_from_world(), then
/// edited cell by cell when a door opens or a wall breaks with nav_set_cost().
struct nav_grid {
  vec2 origin{};            ///< Top-left corner of cell (0, 0) in the world.
  vec2 cell_size{16.0f, 16.0f}; ///< Size of one cell, in world units.
  i32 width = 0;            ///< Number of columns.
  i32 height = 0;           ///< Number of rows.
  /// Cost to enter each cell, row by row: `cost[y * width + x]`. 0 is an obstacle,
  /// 1 is normal, greater means hard to cross (mud, shallow water): the path avoids
  /// it if a cheaper detour exists.
  std::vector<u8> cost;
};

/// Creates an empty grid where every cell has the same cost.
/// @param origin Top-left corner in the world.
/// @param cell_size Cell size.
/// @param width Number of columns.
/// @param height Number of rows.
/// @param cost Cost of every cell. 1 is passable, 0 is an obstacle.
/// @return The grid.
nav_grid nav_grid_make(vec2 origin, vec2 cell_size, i32 width, i32 height, u8 cost = 1);

/// Builds a grid covering `area` from the obstacles currently in the world: every cell
/// overlapping a **non-trigger** collider (box, circle, or a `collider_tiles` cell
/// other than `tile_none`) with `layer & mask != 0` is an obstacle.
///
/// Use `mask` to take only walls and ignore characters and monsters:
/// `nav_grid_from_world(ctx, level_bounds(ctx, lv), {16, 16}, layer_walls)`.
/// @param ctx Engine context.
/// @param area World region to cover.
/// @param cell_size Cell size, usually equal to the tilemap cell.
/// @param mask Collision layers treated as obstacles.
/// @return The grid.
nav_grid nav_grid_from_world(const context &ctx, rect area, vec2 cell_size, u32 mask = 0xFFFFFFFFu);

/// The cell containing a point in the world (may lie outside the grid).
/// @param grid Grid.
/// @param pos Point.
/// @return Cell.
cell nav_cell_at(const nav_grid &grid, vec2 pos);

/// Center of a cell in the world.
/// @param grid Grid.
/// @param c Cell.
/// @return Cell center.
vec2 nav_cell_center(const nav_grid &grid, cell c);

/// Cost of a cell. A cell outside the grid is 0.
/// @param grid Grid.
/// @param c Cell.
/// @return Cost, 0 is an obstacle.
u8 nav_cost(const nav_grid &grid, cell c);

/// Sets the cost of a cell. A cell outside the grid is ignored.
/// @param grid Grid.
/// @param c Cell.
/// @param cost Cost, 0 is an obstacle.
void nav_set_cost(nav_grid &grid, cell c, u8 cost);

/// Marks every cell overlapping `area` as an obstacle: a chest that was just placed, a
/// door that just closed.
/// @param grid Grid.
/// @param area World region.
/// @param cost Cost assigned to those cells, 0 (obstacle) by default.
void nav_set_area(nav_grid &grid, rect area, u8 cost = 0);

/// Options for nav_find_path().
struct nav_path_opts {
  /// Allow diagonal moves. Turn off for games that move in 4 directions only.
  bool diagonal = true;
  /// Move diagonally past the corner of an obstacle. Off (default) means diagonal moves
  /// happen only when both neighboring cells are free, so monsters do not scrape wall corners.
  bool cut_corners = false;
  /// Simplify the path: drop intermediate points when a straight line from the previous
  /// point to the next is clear, so monsters move diagonally instead of stepping cell by cell.
  bool smooth = true;
  /// When the target is unreachable: still return the path to the closest cell found
  /// to the target (the function returns `false`). Suits monsters chasing a
  /// player standing somewhere unreachable.
  bool partial = true;
  /// Maximum number of cells examined, so an unreachable target on a large map
  /// does not slow down the frame.
  i32 max_nodes = 20000;
};

/// Finds the shortest path (A*) from `from` to `to`.
///
/// `out` is cleared and then receives the points to pass through, in the world, not
/// including the starting point; the last point is `to` when it is reachable. Use with nav_steer().
/// @code
/// std::vector<njin::vec2> path;
/// if (njin::nav_find_path(g.nav, enemy_pos, player_pos, path)) { ... }
/// @endcode
/// @param grid Grid.
/// @param from Starting point.
/// @param to Target point.
/// @param out Receives the path.
/// @param opts Options.
/// @return `true` if the target was reached. `false` otherwise (in which case `out` may
/// contain the path to the closest spot, see nav_path_opts::partial).
bool nav_find_path(const nav_grid &grid, vec2 from, vec2 to, std::vector<vec2> &out,
                   const nav_path_opts &opts = {});

/// Whether the straight segment from `a` to `b` passes through any obstacle cell.
/// @param grid Grid.
/// @param a Start point.
/// @param b End point.
/// @return `true` if every cell it passes through is passable.
bool nav_line_clear(const nav_grid &grid, vec2 a, vec2 b);

/// A path being followed, used with nav_steer().
struct nav_agent {
  std::vector<vec2> path; ///< Points to pass through, from nav_find_path().
  i32 next = 0;           ///< Point currently being headed to.
  f32 reach = 3.0f;       ///< How close to a point counts as having reached it.

  /// Sets a new path and restarts from the first point. @param p Path.
  void set(std::vector<vec2> p) {
    path = std::move(p);
    next = 0;
  }
  /// Whether the whole path has been walked. @return `true` if no points are left.
  bool done() const { return next >= (i32)path.size(); }
};

/// Direction to move to follow the path, of length 1, or `{0, 0}` at the end.
/// Automatically advances to the next point when close to the current one. Assign the
/// result to `topdown_body::input.move`.
/// @param agent Path being followed.
/// @param pos Current position.
/// @return Direction to move.
vec2 nav_steer(nav_agent &agent, vec2 pos);
/// @}
} // namespace njin
