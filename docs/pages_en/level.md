# Levels from Tiled and LDtk {#level}

Draw your levels in an editor instead of hard-coding them. njin reads the files of the two popular
2D editors with the same function. Not using an editor? Write the map as text, one character per tile: see
@ref tilemap.

| Editor | File | Notes |
|---|---|---|
| [Tiled](https://www.mapeditor.org) | `.tmx` (default, XML), `.tmj` / `.json` | Tilesets in separate files (`.tsx`, `.tsj`) are read too |
| [LDtk](https://ldtk.io) | `.ldtk` | Also when "Save levels to separate files" is on (`.ldtkl`) |

@include level.cpp

## What gets created

njin::level_load() reads the file and creates entities right away:

| In the editor | Becomes an entity |
|---|---|
| Tile layer (Tiled tile layer, LDtk Tiles / AutoLayer) | njin::tilemap. If one layer uses several tilesets, each tileset gets its own tilemap |
| LDtk IntGrid layer | A **hidden** tilemap, the tile values are the IntGrid values (1, 2, ...). Read with njin::tilemap_get(), for example a water tile is 2 |
| Object (Tiled) / entity (LDtk) | An entity with a njin::transform at the object's **center**, and a njin::level_object |
| Tile object (Tiled), entity with a visible tile (LDtk) | Also gets a njin::sprite |
| Image layer (Tiled) | A sprite |

Draw order follows the editor: the bottom layer has njin::sprite::layer equal to `level_desc::layer_base`, and each
layer above it is one higher.

Tilesets with margins (margin, padding) and spacing between tiles (spacing), horizontally and vertically flipped tiles, opacity
and the layer's offset are all kept correct. Tileset images are sampled with
`filter_nearest` by default for sharp pixel art (change it with `level_desc::filter`).

## Objects and prefabs

Every object carries a njin::level_object:

| Field | Tiled | LDtk |
|---|---|---|
| `name` | Name | `iid` |
| `type` | Class (or Type in older versions) | Entity name |
| `size` | Width, Height | Width, Height |
| `props` | Custom properties | Fields |
| `points` | Polygon, polyline (relative to the object's position) | |

**If a prefab has the same name as `type`, the entity is built with that prefab** (see @ref prefabs).
njin::level_object is attached *before* the builder function runs, so the builder can read the properties:
set `value = 5` for a coin in the editor, and the builder reads `obj.props["value"].int_or(1)`.
Properties are njin::json_value (see @ref json): strings, numbers, bools, and even nested objects such as
LDtk's Point fields (`props["target"]["cx"]`).

With no prefab, the object is still an entity with a transform and a njin::level_object. Find it with
njin::level_find(), by name and then by class: handy for spawn points, camera points, trigger areas.

## Obstacles

A layer is an obstacle if its name is in `level_desc::solid_layers`. If the list is left empty, the
default rule applies: a layer with the property `solid = true` (Tiled), or a name containing "collision",
"collide", "solid" or "wall".

| Layer type | Becomes |
|---|---|
| Tile layer, IntGrid | A njin::collider `collider_tiles`: every non-empty tile blocks njin::collision_move() and raycasts |
| Object layer | Objects without a prefab: rectangles become box colliders, ellipses become circle colliders |

The colliders created take their `layer` and `mask` from `level_desc::solid`. With LDtk, an auto-layer tile drawn over the
IntGrid is only decoration; collision comes from the IntGrid value.

## Multiple levels and unloading

- njin::level_load_ldtk() picks a level by name; njin::level_list_ldtk() lists the names.
  `level_desc::use_world_position` puts a level at its position in the LDtk world so you can load several
  levels side by side.
- By default every entity of a level belongs to the running scene: when you leave the scene they are destroyed **and the level's
  images are freed**. Call njin::level_unload() to drop a level earlier.
- njin::level_size() and njin::level_origin() give the size and position, for limiting the camera.
- njin::level_properties() returns the properties of the whole map (Tiled) or of the level (LDtk).

## Square grids only

njin makes top-down and platformer games, so it only accepts **square-grid** maps (Tiled: Orientation
"Orthogonal"; LDtk is always a square grid). Tiled's isometric, staggered or hexagonal maps are
refused: njin::level_load() returns a handle with id 0 and writes the reason to the log, instead of drawing it wrong.

## Not supported yet

The following produce a warning in the log instead of failing silently: zstd compression (save as CSV, zlib or
gzip instead), diagonally rotated tiles (drawn without rotation), tilesets made of multiple separate images, animated tiles, Tiled's
object templates, and multiple worlds in one LDtk project.

## Collision shapes and tile animation

- **Tiled**: set a string property `collision` (or `shape`, or the tile's class) on the tile in the tileset:
  `solid`, `none`, `one_way`, `slope_r`, `slope_l`, `slope_r_low`, `slope_r_high`, `slope_l_low`,
  `slope_l_high`. Animations drawn with the tileset's animation editor play at the right pace.
- **LDtk**: name the IntGrid values with the names above, or attach an enum tag or custom data to the tileset's
  tiles.

See @ref platformer.
