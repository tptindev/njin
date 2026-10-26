#pragma once
#include "_math.h"
#include "_tilemap.h"
#include "_types.h"
#include <vector>

namespace njin {

/// @addtogroup grp_place
/// @{

/// A cell no thing holds, see place_grid::owner.
constexpr u32 place_none = 0;

/// A grid to put things several cells big on: buildings in a village builder,
/// crops on a farm, items in a grid inventory. Each cell remembers which thing
/// holds it.
///
/// A thing is a block of `w` x `h` cells whose top-left cell is `at`; turning it
/// 90 degrees swaps `w` and `h`. Each thing carries an `id` other than
/// place_none that the game chooses, e.g. its index in the array of buildings
/// plus 1.
///
/// This grid does not block paths: to make enemies walk around a building, also
/// mark its place_rect() on a njin::nav_grid with nav_set_area().
/// @code
/// njin::place_grid village = njin::place_grid_make({0, 0}, {32, 32}, 40, 40);
/// // Dragging a 3x3 house: snap to the cells under the mouse, colour it green or red.
/// const njin::cell at = njin::place_snap(village, njin::scr2w(ctx, njin::mouse_pos(ctx)), 3, 3);
/// const bool ok = njin::place_fits(village, at, 3, 3);
/// njin::draw_rect(ctx, njin::place_rect(village, at, 3, 3), ok ? green : red);
/// if (ok && njin::mouse_pressed(ctx, njin::mouse_left))
///   njin::place_put(village, at, 3, 3, house_id);
/// @endcode
struct place_grid {
  vec2 origin{};                ///< Top-left corner of cell (0, 0) in the world.
  vec2 cell_size{16.0f, 16.0f}; ///< Size of one cell, in world units.
  i32 width = 0;                ///< Number of columns.
  i32 height = 0;               ///< Number of rows.
  /// The thing holding each cell, row by row: `owner[y * width + x]`. place_none is empty.
  std::vector<u32> owner;
};

/// Makes an empty grid.
/// @param origin Top-left corner in the world.
/// @param cell_size Cell size.
/// @param width Number of columns.
/// @param height Number of rows.
/// @return The grid.
place_grid place_grid_make(vec2 origin, vec2 cell_size, i32 width, i32 height);

/// The cell holding a point of the world (may be outside the grid).
/// @param grid Grid.
/// @param pos Point.
/// @return Cell.
cell place_cell_at(const place_grid &grid, vec2 pos);

/// The top-left cell for a `w` x `h` thing whose centre is nearest `pos`: snaps
/// the thing being dragged to the cells, with the mouse in its middle rather
/// than at its corner. May be outside the grid; check it with place_fits().
/// @param grid Grid.
/// @param pos Point, usually the mouse in the world.
/// @param w Columns of the thing.
/// @param h Rows of the thing.
/// @return Top-left cell.
cell place_snap(const place_grid &grid, vec2 pos, i32 w, i32 h);

/// The world area of a `w` x `h` thing put at `at`: to draw it, to place a
/// sprite, or to mark it on a njin::nav_grid.
/// @param grid Grid.
/// @param at Top-left cell.
/// @param w Columns.
/// @param h Rows.
/// @return Rectangle in the world.
rect place_rect(const place_grid &grid, cell at, i32 w, i32 h);

/// Which thing holds cell `c`.
/// @param grid Grid.
/// @param c Cell.
/// @return The thing's `id`, or place_none if the cell is empty or outside the grid.
u32 place_owner(const place_grid &grid, cell c);

/// Whether a `w` x `h` thing can go at `at`: wholly inside the grid, and every
/// cell empty or held by `ignore`. Pass the `id` of the thing being moved as
/// `ignore`, so it does not collide with its own old spot.
/// @param grid Grid.
/// @param at Top-left cell.
/// @param w Columns, at least 1.
/// @param h Rows, at least 1.
/// @param ignore Thing treated as absent.
/// @return `true` if it fits.
bool place_fits(const place_grid &grid, cell at, i32 w, i32 h, u32 ignore = place_none);

/// Puts thing `id` on the `w` x `h` cells from `at`, if place_fits() (with
/// `ignore` set to `id`). Putting an `id` already on the grid moves it: its old
/// spot is freed.
/// @param grid Grid.
/// @param at Top-left cell.
/// @param w Columns.
/// @param h Rows.
/// @param id Thing, other than place_none.
/// @return `true` if it was put; on `false` the grid is unchanged.
bool place_put(place_grid &grid, cell at, i32 w, i32 h, u32 id);

/// Takes thing `id` off the grid: all its cells become empty.
/// @param grid Grid.
/// @param id Thing.
/// @return Number of cells freed, 0 if the thing is not on the grid.
i32 place_remove(place_grid &grid, u32 id);

/// Finds the first free spot for a `w` x `h` thing, scanning rows from the top,
/// each row from the left: picking an item up into a bag, placing a building
/// just bought.
/// @param grid Grid.
/// @param w Columns.
/// @param h Rows.
/// @param out Receives the top-left cell when found.
/// @return `true` if found.
bool place_find_free(const place_grid &grid, i32 w, i32 h, cell &out);
/// @}
} // namespace njin
