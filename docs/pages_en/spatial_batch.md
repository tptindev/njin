# Draw only what the camera sees: instances by cell {#spatial_batch}

draw_instanced3d() draws a whole stretch of a buffer in one call, but the graphics card still processes every instance
in that stretch, including those behind the camera. With a forest or a city of tens of thousands of things of which the
camera sees one corner, most of that work is wasted. `njin_spatial_batch.h` deals with it by splitting the ground into
cells: put the instances in the buffer in cell order **once**, then every frame pick the cells to draw and get back the
matching stretches of the buffer. The same way picks a level of detail: near cells drawn with a fine shape, far ones
with a low-poly one.

You should know first: @ref graphics_3d, the Instancing section. This works with the 2D draw_instanced() too, since it
also takes a first instance and a count.

## Example: 40,000 trees

@include spatial_batch.cpp

## Three steps

| Step | When | What |
|---|---|---|
| 1. Grid | Once | njin::batch_grid2d: corner `origin`, cell size `cell_size`, `cols` columns and `rows` rows |
| 2. Order the buffer | On load, or when things are added or removed | Each instance belongs to the cell holding its centre (`grid.cell_at()`). Write the buffer by cell: every instance of cell 0, then cell 1... `offsets[c]` is the first instance of cell `c`, the last element is the total number of instances |
| 3. Pick cells | Every frame, for every camera | An array `wanted`, one number per cell (not 0 draws it), then batch_instance_ranges() returns the stretches `[first, second)` for draw_instanced3d() |

Picked cells next to each other merge into one stretch, so a visible area usually costs only a few draw calls. Empty
cells are fine.

The cell size is a trade-off: small cells follow the view more closely but give more stretches (more draw calls); big
cells the reverse. Start with cells a few times the size of the largest thing, then measure.

## The view

njin::batch_view2d describes the part of the ground a camera sees, on the same plane as the grid: x, z of the world in
a 3D scene, x, y in a 2D one. Every number is in the game's unit.

| Field | Meaning |
|---|---|
| `lo`, `hi` | The visible rectangle. It must **cover all** the camera sees: too wide only draws extra, too narrow and things at the edge of the screen vanish |
| `overhang` | How far a thing reaches out of the cell its centre is in, at most (the largest instance radius) |
| `focus` | The point distances for the level of detail are measured from, usually the camera position |

batch_cell_visible() tells whether a cell touches the view. batch_cell_detailed() tells whether a cell is within a
radius of `focus`, and does not look at the view: combine the two as in the example for two levels of detail, one
`wanted` array each.

This is a coarse rectangle test, not a 3D frustum test or occlusion: something inside the rectangle but outside the
frame is still drawn. Each camera (main screen, minimap, mirror) keeps a view of its own and shares the buffer.

## Leaving a stretch out

The third parameter of batch_instance_ranges() is the stretches **not** to draw, for example a house being drawn on its
own, cut open:

@code
const njin::instance_range excluded[] = {{house_first, house_first + house_count}};
for (const auto &[from, to] : njin::batch_instance_ranges(offsets, wanted, excluded))
  njin::draw_instanced3d(ctx, house_model, buffer, from, to - from);
@endcode

The stretches left out may overlap and need not be sorted.

## What it does not do

- It does not hold or change the buffer: the game orders the instances, calls instance_buffer_upload() and keeps
  `offsets` matching the buffer.
- It does not pick the shape for each level of detail: the game decides what to draw for each `wanted` array.
- Wrong input gives an empty result instead of a guess: `offsets` without exactly `wanted.size() + 1` elements, or
  decreasing somewhere.

The functions only read their input and keep nothing between calls, so they can be called for several cameras, and on
another thread as long as nobody changes the input meanwhile. Keep the `wanted` arrays across frames to avoid
allocating again.
