#pragma once
#include "_types.h"
#include <span>
#include <utility>
#include <vector>

namespace njin {

/// @addtogroup grp_instancing
/// @{

/// A grid splitting the ground into cells, to group instances by the cell their
/// centre is in.
///
/// The plane is the game's: x, z of the world in a 3D scene, x, y in a 2D one.
/// Every number (origin, cell_size, the view, radii) is in one unit, the game's.
/// The grid is only numbers and holds no buffer or camera. See @ref spatial_batch.
struct batch_grid2d {
  vec2 origin{};          ///< The smallest corner (smallest x, y) of cell 0.
  vec2 cell_size{1, 1};   ///< Size of a cell on both axes, greater than 0.
  i32 cols = 0;           ///< Number of columns (along the first axis).
  i32 rows = 0;           ///< Number of rows (along the second axis).
  /// The number of cells, `cols * rows`. 0 if the grid is invalid (a size or
  /// count that is not positive, numbers that are not finite, too many cells).
  /// @return Number of cells.
  i32 count() const;
  /// The cell holding point `p`: `row * cols + column`. A point outside the grid
  /// belongs to the nearest border cell.
  /// @param p A point on the plane, usually an instance's centre.
  /// @return The cell index, -1 if the grid is invalid or `p` is not finite.
  i32 cell_at(vec2 p) const;
};

/// The part of the plane a camera sees, built by the game from its camera every frame.
///
/// Each camera (main screen, minimap, mirror) keeps a view of its own. This is a
/// coarse rectangle test, not a replacement for a 3D frustum test or occlusion: the
/// rectangle must **cover all** the camera sees; too wide only costs extra drawing.
struct batch_view2d {
  vec2 lo{};    ///< Smallest corner of the visible rectangle.
  vec2 hi{};    ///< Largest corner; not smaller than `lo` on either axis.
  vec2 focus{}; ///< Point distances are measured from for batch_cell_detailed(), usually the camera position.
  /// How far a thing reaches out of the cell its centre is in, at most (the
  /// largest instance radius). Too small and things at the edge of the screen
  /// vanish. Not negative.
  f32 overhang = 0;
};

/// Whether cell `cell` falls in the view: whether the cell's rectangle, grown by
/// `overhang`, touches the rectangle `lo` .. `hi`.
/// @param grid The grid.
/// @param view The camera's view.
/// @param cell Cell index.
/// @return false also when the cell, grid or view is invalid.
bool batch_cell_visible(const batch_grid2d &grid, const batch_view2d &view, i32 cell);

/// Whether cell `cell` is near: whether the distance from `view.focus` to the
/// cell's rectangle is below `radius`. For choosing a level of detail (a fine shape
/// up close, a low-poly one far away). Does not look at the view; combine it with
/// batch_cell_visible() when needed.
/// @param grid The grid.
/// @param view The view, only `focus` is used.
/// @param cell Cell index.
/// @param radius Radius of the near area, greater than 0.
/// @return false also when the cell, grid, `focus` or `radius` is invalid.
bool batch_cell_detailed(const batch_grid2d &grid, const batch_view2d &view, i32 cell, f32 radius);

/// A run of instances `[first, second)` in a buffer: draw it with
/// `draw_instanced3d(ctx, mesh, buffer, first, second - first)`.
using instance_range = std::pair<u32, u32>;

/// The runs of instances to draw, from the chosen cells.
///
/// The buffer must hold its instances in cell order: every instance of cell 0,
/// then cell 1, ... `offsets[c]` is the first instance of cell `c` and
/// `offsets[c + 1]` is where that cell ends, so `offsets` has `wanted.size() + 1`
/// elements and never decreases; empty cells are fine. Chosen cells next to each
/// other merge into one run, then the runs in `excluded` are cut out.
///
/// The function changes nothing but its result, so it can be called for several
/// cameras and on another thread, as long as nobody changes its inputs meanwhile.
/// @param offsets Where each cell starts in the buffer, plus where the last ends.
/// @param wanted One number per cell: not 0 draws that cell.
/// @param excluded Runs not to draw (for example a thing being drawn on its own),
/// in any order, possibly overlapping. Empty or reversed runs are ignored.
/// @return Increasing runs that do not overlap and do not pass `offsets.back()`.
/// Empty if `offsets` has the wrong size or decreases somewhere.
std::vector<instance_range> batch_instance_ranges(std::span<const u32> offsets,
    std::span<const u8> wanted, std::span<const instance_range> excluded = {});

/// @}

} // namespace njin
