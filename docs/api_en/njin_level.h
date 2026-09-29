#pragma once
#include "_comps.h"
#include "_types.h"
#include "njin_collision.h"
#include "njin_draw.h"
#include "njin_json.h"
#include <entt/entity/entity.hpp>
#include <string>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_level
/// @{

/// Identifies a loaded level, returned by level_load().
///
/// `id == 0` is an invalid handle (the load failed). An unloaded level is also ignored.
struct level_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// How to load a level, used with level_load() and level_load_ldtk().
struct level_desc {
  /// World position of the level's top-left corner.
  vec2 origin{};
  /// For LDtk: add the level's position in the world (`worldX`, `worldY`), to
  /// load several adjacent levels as they appear in the editor.
  bool use_world_position = false;
  /// Draw layer (njin::sprite::layer) of the bottom layer. Each layer above it is
  /// one higher: the `i`-th layer from the bottom draws at `layer_base + i`.
  i32 layer_base = 0;
  /// Names of the layers that act as obstacles. Their cells become `collider_tiles`, and objects
  /// without a prefab become box or circle colliders. If empty, the default rule applies:
  /// a layer with the property `solid = true` (Tiled), or whose name contains
  /// "collision", "collide", "solid" or "wall" (case-insensitive).
  std::vector<std::string> solid_layers{};
  /// Collider template for obstacles: `layer` and `mask` are kept, the shape is reset.
  collider solid{};
  /// How the tileset image is sampled. Defaults to `filter_nearest`: crisp pixel art,
  /// and no blurry seams between cells.
  texture_filter filter = filter_nearest;
  /// Attach every entity of the level to the running scene (njin::scene_owned). When the
  /// scene is left, the entities are destroyed and the level is unloaded automatically.
  bool scene_owned = true;
};

/// An object placed in the editor: spawn point, door, zone, monster...
///
/// Each object becomes an entity with a transform (the object's center) and this
/// component. If a prefab matches `type` by name (the object's class in Tiled, the
/// entity name in LDtk), the entity is built with that prefab; this component is
/// attached **before** the builder runs, so the builder can read the properties:
/// @code
/// void build_door(njin::context &ctx, entt::entity e) {
///   const auto &obj = njin::world(ctx).get<njin::level_object>(e);
///   const char *target = obj.props["target"].string_or("start");
///   ...
/// }
/// @endcode
struct level_object {
  std::string name; ///< Name (Tiled: name; LDtk: iid).
  std::string type; ///< Class (Tiled: class or type; LDtk: entity name).
  vec2 size{};      ///< Size, in pixels. 0 for points.
  /// Vertices of a polygon or polyline, relative to `transform.pos`. Empty for
  /// rectangles, ellipses and points.
  std::vector<vec2> points{};
  bool ellipse = false; ///< The object is an ellipse (Tiled).
  bool closed = true;   ///< For `points`: a polygon (closed) or a polyline (open).
  json_value props{};   ///< Custom properties (Tiled: properties; LDtk: fields).
  level_handle level{}; ///< The level containing the object.
};

/// Loads a map and creates entities for it.
///
/// Accepts files from **Tiled** (`.tmx`, `.tmj`, or `.json` exported from Tiled) and
/// **LDtk** (`.ldtk`, loads the first level; use level_load_ldtk() to choose
/// the level). Tilesets, images and separate-file levels are looked up next to the map file.
///
/// Creates:
/// - for each tile layer: an entity with a transform and njin::tilemap (a layer using
///   several tilesets gets one entity per tileset); an obstacle layer also gets a
///   njin::collider `collider_tiles`;
/// - for each LDtk IntGrid layer: a hidden tilemap whose cell values are the IntGrid values
///   (1, 2, ...), read with tilemap_get(); an obstacle layer also gets a collider;
/// - for each object: an entity with a transform and njin::level_object, built with the
///   prefab matching its class if there is one. A tile object also gets
///   njin::sprite.
///
/// Accepts only square-grid (orthogonal) maps, the kind that top-down and
/// platformer games use; isometric and hexagonal maps are refused (returns a handle with id 0,
/// a log is written). Supports infinite maps, nested layers, CSV, base64,
/// zlib and gzip data. Not yet supported (a warning is logged): zstd compression, diagonally
/// flipped cells, multi-image tilesets, animated tiles.
/// @param ctx Engine context.
/// @param path Path of the map file.
/// @param desc How to load.
/// @return Handle of the level, or a handle with id 0 on error (a log is written).
level_handle level_load(context &ctx, const char *path, const level_desc &desc = {});

/// Loads a specific level from an LDtk project.
/// @param ctx Engine context.
/// @param path Path of the `.ldtk` file.
/// @param level Level name (identifier), or null for the first level.
/// @param desc How to load.
/// @return Handle of the level, or a handle with id 0 on error.
level_handle level_load_ldtk(context &ctx, const char *path, const char *level,
                             const level_desc &desc = {});

/// Names of every level in an LDtk project, in editor order.
/// @param path Path of the `.ldtk` file.
/// @param out Receives the names (appended at the end).
/// @return `false` if the file could not be read.
bool level_list_ldtk(const char *path, std::vector<std::string> &out);

/// Destroys every entity the level created and frees its images. A level bound to a
/// scene (the default) is unloaded automatically when the scene is left.
/// @param ctx Engine context.
/// @param level Level. An invalid handle is ignored.
void level_unload(context &ctx, level_handle level);

/// Level size, in pixels. @param ctx Engine context. @param level Level.
/// @return Size, or `{0, 0}` if the handle is invalid.
vec2 level_size(const context &ctx, level_handle level);

/// Top-left corner of the level in the world. @param ctx Engine context.
/// @param level Level. @return Position.
vec2 level_origin(const context &ctx, level_handle level);

/// Custom properties of the whole map (Tiled) or of the level (LDtk).
/// @param ctx Engine context.
/// @param level Level.
/// @return A JSON object, or a null value if the handle is invalid.
const json_value &level_properties(const context &ctx, level_handle level);

/// Finds the first object of the level whose name (or, if no name matches,
/// class) is `name`. Handy for spawn points: `level_find(ctx, lv, "spawn")`.
/// @param ctx Engine context.
/// @param level Level.
/// @param name Name or class.
/// @return Entity, or `entt::null`.
entt::entity level_find(context &ctx, level_handle level, const char *name);
/// @}
} // namespace njin
