#pragma once
#include "njin_internal_only.h"

#include "_tilemap.h"
#include "njin_level.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace njin {
// One loaded level. Handle id N maps to levels[N - 1]; slots are never reused.
struct level_slot {
  bool alive = false;
  scene_handle scene{}; // unloaded automatically when this scene is left
  std::vector<entt::entity> entities;
  std::vector<texture_handle> textures; // loaded for this level, freed with it
  vec2 origin{};
  vec2 size{};
  json_value props;
};

struct level_store {
  std::vector<level_slot> levels;
};

// Frees the textures of every level owned by `scene`, whose entities were just
// destroyed with the scene. Called by scene_store_apply.
void level_store_scene_exit(njin_ctx &ctx, scene_handle scene);

// Where a tile image comes from: one texture cut into a grid.
struct level_tileset {
  texture_handle texture{};
  vec2 tile_size{};
  f32 margin = 0.0f;
  f32 spacing = 0.0f;
  i32 columns = 0;
  // Collision shapes (tile id -> shape) and animations, copied onto every
  // tilemap drawn from this tileset.
  std::vector<std::pair<i32, tile_shape>> shapes;
  std::unordered_map<i32, tile_anim> anims;

  // Source rectangle of tile `id` in the texture.
  rect source(i32 id) const {
    const i32 cols = columns > 0 ? columns : 1;
    return rect{{margin + (f32)(id % cols) * (tile_size.x + spacing),
                 margin + (f32)(id / cols) * (tile_size.y + spacing)},
                tile_size};
  }
};

// One tile layer as it is built: cells go into tilemaps per tileset, and a
// cell already taken in a tilemap spills into another tilemap drawn just
// above (LDtk auto-layers can stack several tiles in one cell).
struct level_tile_layer {
  std::string name;
  vec2 origin{};
  bool visible = true;
  bool solid = false;
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f};
  i32 draw_layer = 0;
};

// Everything the Tiled and LDtk loaders share: textures, entity creation,
// solidity rules and bookkeeping for level_unload.
struct level_builder {
  njin_ctx &ctx;
  const level_desc &desc;
  level_handle handle;
  level_slot &slot;
  std::string dir; // folder of the file being read, for relative paths
  std::unordered_map<std::string, texture_handle> loaded;
  i32 next_draw_layer = 0;
  bool warned_flip_diagonal = false;

  // Loads an image relative to `dir` (or `base` when given), once per level.
  texture_handle texture(const std::string &relative, const std::string &base = {});
  // Whether a layer named `name` blocks, given its own `solid` property.
  bool is_solid(const std::string &name, bool solid_property) const;
  // Draw layer for the next map layer, bottom-up.
  i32 take_draw_layer() { return desc.layer_base + next_draw_layer++; }
  // Records an entity as part of this level and gives it scene ownership.
  void own(entt::entity entity);

  // Adds `values` (cell -> tile value, flip bits included) as tilemaps.
  void add_tiles(const level_tile_layer &layer, const level_tileset &tileset,
                 const std::vector<std::pair<cell, i32>> &values);
  // Adds an invisible tilemap holding raw values (LDtk IntGrid), with a
  // collision shape per value.
  void add_value_grid(const level_tile_layer &layer, vec2 cell_size,
                      const std::vector<std::pair<cell, i32>> &values,
                      const std::vector<std::pair<i32, tile_shape>> &shapes = {});
  // Spawns an object: through the prefab named after its type when one is
  // registered, else as a plain entity (with a sprite when `sprite_source`
  // has a texture, and a collider when `solid`).
  entt::entity add_object(level_object object, const transform &at, bool solid,
                          const sprite *tile_sprite);
};

// Directory part of a path, with a trailing separator, or empty.
std::string level_dir_of(const std::string &path);
// Decodes base64 (whitespace ignored).
bool level_base64(std::string_view text, std::vector<u8> &out);
// Inflates zlib ("zlib") or gzip ("gzip") data; "" copies as is.
bool level_decompress(const std::vector<u8> &in, const char *method, std::vector<u8> &out);

// The LDtk loader, in njin_ldtk.cpp.
level_handle level_load_ldtk_file(njin_ctx &ctx, const char *path, const char *level,
                                  const level_desc &desc);
// Starts a new level slot; the loaders fill it through a level_builder.
level_handle level_begin(njin_ctx &ctx);
// Undoes a level whose load failed halfway.
void level_abort(njin_ctx &ctx, level_handle level);
level_slot *level_slot_of(njin_ctx &ctx, level_handle level);
} // namespace njin
