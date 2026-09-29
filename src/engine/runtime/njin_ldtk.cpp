#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_file.h"
#include "njin_level_impl.h"
#include "njin_log.h"
#include "njin_path.h"
#include <cmath>
#include <cstring>

namespace njin {
namespace {
bool read_json(const std::string &path, json_value &out) {
  std::string text;
  if (!file_read(path.c_str(), text)) {
    NJIN_WARN("level: cannot read %s", path.c_str());
    return false;
  }
  std::string error;
  if (!json_parse(text, out, &error)) {
    NJIN_WARN("level: %s: %s", path.c_str(), error.c_str());
    return false;
  }
  return true;
}

// LDtk field instances ([{__identifier, __value}]) as one object.
json_value fields_object(const json_value &fields) {
  json_value out = json_value::make_object();
  for (const json_value &f : fields.items)
    out.set(f["__identifier"].string_or(""), f["__value"]);
  return out;
}

struct ldtk_tileset {
  i32 uid = 0;
  level_tileset tiles;
  f32 grid = 0.0f;
  bool usable = false;
};

struct ldtk_loader {
  level_builder &b;
  std::string where;
  std::vector<ldtk_tileset> tilesets;
  json_value project_defs;

  const ldtk_tileset *tileset(i32 uid) const {
    for (const ldtk_tileset &t : tilesets)
      if (t.uid == uid && t.usable)
        return &t;
    return nullptr;
  }

  void load_tilesets(const json_value &project) {
    for (const json_value &def : project["defs"]["tilesets"].items) {
      ldtk_tileset t;
      t.uid = def["uid"].int_or(0);
      const char *rel = def["relPath"].string_or(nullptr);
      if (rel == nullptr) { // embedded atlas or a tileset without image
        tilesets.push_back(t);
        continue;
      }
      t.grid = def["tileGridSize"].f32_or(16.0f);
      t.tiles.texture = b.texture(rel);
      t.tiles.tile_size = {t.grid, t.grid};
      t.tiles.margin = def["padding"].f32_or(0.0f);
      t.tiles.spacing = def["spacing"].f32_or(0.0f);
      t.tiles.columns = (i32)((def["pxWid"].f32_or(0.0f) - 2.0f * t.tiles.margin + t.tiles.spacing) /
                              (t.grid + t.tiles.spacing));
      t.usable = t.tiles.texture.id != 0 && t.tiles.columns > 0;
      // Collision shapes from enum tags or custom data named after a shape
      // ("one_way", "slope_r"...).
      for (const json_value &tag : def["enumTags"].items) {
        tile_shape shape{};
        if (!tile_shape_from_name(tag["enumValueId"].string_or(nullptr), shape))
          continue;
        for (const json_value &id : tag["tileIds"].items)
          t.tiles.shapes.push_back({id.int_or(0), shape});
      }
      for (const json_value &data : def["customData"].items) {
        tile_shape shape{};
        if (tile_shape_from_name(data["data"].string_or(nullptr), shape))
          t.tiles.shapes.push_back({data["tileId"].int_or(0), shape});
      }
      tilesets.push_back(t);
    }
  }

  // Tile id from the pixel position of its source in the tileset.
  static i32 tile_index(const ldtk_tileset &t, const json_value &src) {
    const f32 step = t.grid + t.tiles.spacing;
    const i32 col = (i32)std::lround((src[(usize)0].f32_or(0.0f) - t.tiles.margin) / step);
    const i32 row = (i32)std::lround((src[(usize)1].f32_or(0.0f) - t.tiles.margin) / step);
    return row * t.tiles.columns + col;
  }

  void tiles(const json_value &list, const json_value &layer, const level_tile_layer &info,
             f32 grid) {
    if (list.size() == 0)
      return;
    i32 uid = layer["overrideTilesetUid"].int_or(0);
    if (uid == 0)
      uid = layer["__tilesetDefUid"].int_or(0);
    const ldtk_tileset *t = tileset(uid);
    if (t == nullptr) {
      NJIN_WARN("level: %s: layer '%s' uses a missing tileset", where.c_str(), info.name.c_str());
      return;
    }
    if (t->grid != grid)
      NJIN_WARN("level: %s: layer '%s' has a %g grid but tileset tiles are %g", where.c_str(),
                info.name.c_str(), grid, t->grid);
    std::vector<std::pair<cell, i32>> values;
    values.reserve(list.size());
    for (const json_value &tile : list.items) {
      const json_value &px = tile["px"];
      const cell c{(i32)std::floor(px[(usize)0].f32_or(0.0f) / grid),
                   (i32)std::floor(px[(usize)1].f32_or(0.0f) / grid)};
      i32 value = tile_index(*t, tile["src"]);
      const i32 flips = tile["f"].int_or(0);
      if ((flips & 1) != 0)
        value |= tile_flip_x;
      if ((flips & 2) != 0)
        value |= tile_flip_y;
      values.push_back({c, value});
    }
    b.add_tiles(info, t->tiles, values);
  }

  void entities(const json_value &layer, vec2 origin, bool solid, i32 draw_layer, bool visible) {
    for (const json_value &ent : layer["entityInstances"].items) {
      level_object obj;
      obj.name = ent["iid"].string_or("");
      obj.type = ent["__identifier"].string_or("");
      obj.size = {ent["width"].f32_or(0.0f), ent["height"].f32_or(0.0f)};
      obj.props = fields_object(ent["fieldInstances"]);
      const vec2 px{ent["px"][(usize)0].f32_or(0.0f), ent["px"][(usize)1].f32_or(0.0f)};
      const vec2 pivot{ent["__pivot"][(usize)0].f32_or(0.5f), ent["__pivot"][(usize)1].f32_or(0.5f)};
      // px is where the pivot sits; the transform goes at the centre.
      const transform at{.pos = origin + px - obj.size * pivot + obj.size * 0.5f};

      sprite tile_sprite{};
      bool has_sprite = false;
      const json_value &tile = ent["__tile"];
      if (tile.is(json_value::object)) {
        if (const ldtk_tileset *t = tileset(tile["tilesetUid"].int_or(0))) {
          tile_sprite.texture = t->tiles.texture;
          tile_sprite.source = rect{{tile["x"].f32_or(0.0f), tile["y"].f32_or(0.0f)},
                                    {tile["w"].f32_or(0.0f), tile["h"].f32_or(0.0f)}};
          tile_sprite.layer = draw_layer;
          tile_sprite.visible = visible;
          has_sprite = true;
        }
      }
      b.add_object(std::move(obj), at, solid, has_sprite ? &tile_sprite : nullptr);
    }
  }

  // IntGrid value -> collision shape, from the value names in the layer's
  // definition ("one_way", "slope_r"...). Unnamed values stay solid.
  std::vector<std::pair<i32, tile_shape>> intgrid_shapes(const json_value &layer) const {
    std::vector<std::pair<i32, tile_shape>> out;
    const i32 uid = layer["layerDefUid"].int_or(-1);
    for (const json_value &def : project_defs["layers"].items) {
      if (def["uid"].int_or(-2) != uid)
        continue;
      for (const json_value &v : def["intGridValues"].items) {
        tile_shape shape{};
        if (tile_shape_from_name(v["identifier"].string_or(nullptr), shape))
          out.push_back({v["value"].int_or(0), shape});
      }
    }
    return out;
  }

  void layer(const json_value &layer, vec2 level_origin) {
    level_tile_layer info;
    info.name = layer["__identifier"].string_or("");
    info.origin = level_origin + vec2{layer["__pxTotalOffsetX"].f32_or(0.0f),
                                      layer["__pxTotalOffsetY"].f32_or(0.0f)};
    info.visible = layer["visible"].bool_or(true);
    info.tint.a = layer["__opacity"].f32_or(1.0f);
    info.solid = b.is_solid(info.name, false);
    info.draw_layer = b.take_draw_layer();
    const f32 grid = layer["__gridSize"].f32_or(16.0f);
    const std::string type = layer["__type"].string_or("");

    if (type == "IntGrid") {
      const i32 width = layer["__cWid"].int_or(0);
      std::vector<std::pair<cell, i32>> values;
      const json_value &csv = layer["intGridCsv"];
      for (usize i = 0; i < csv.items.size() && width > 0; i++) {
        const i32 v = csv.items[i].int_or(0);
        if (v != 0)
          values.push_back({cell{(i32)i % width, (i32)i / width}, v});
      }
      if (!values.empty()) {
        level_tile_layer grid_info = info;
        grid_info.visible = false;
        b.add_value_grid(grid_info, {grid, grid}, values, intgrid_shapes(layer));
      }
      // The tiles an auto-layer rule drew on top of the grid are only looks.
      level_tile_layer look = info;
      look.solid = false;
      tiles(layer["autoLayerTiles"], layer, look, grid);
    } else if (type == "Tiles") {
      tiles(layer["gridTiles"], layer, info, grid);
    } else if (type == "AutoLayer") {
      tiles(layer["autoLayerTiles"], layer, info, grid);
    } else if (type == "Entities") {
      entities(layer, info.origin, info.solid, info.draw_layer, info.visible);
    }
  }
};
} // namespace

bool level_list_ldtk(const char *path, std::vector<std::string> &out) {
  if (path == nullptr)
    return false;
  json_value project;
  if (!read_json(asset_path(path), project))
    return false;
  for (const json_value &lv : project["levels"].items)
    out.push_back(lv["identifier"].string_or(""));
  return true;
}

level_handle level_load_ldtk_file(context &ctx, const char *path, const char *level,
                                  const level_desc &desc) {
  json_value project;
  if (!read_json(path, project))
    return level_handle{};
  const json_value &levels = project["levels"];
  if (!levels.is(json_value::array) || levels.size() == 0) {
    NJIN_WARN("level: %s is not an LDtk project, or has no levels", path);
    return level_handle{};
  }
  if (project["worldLayout"].is(json_value::null) && project["worlds"].size() > 0)
    NJIN_WARN("level: %s uses multiple worlds; only the top-level levels are read", path);

  const json_value *chosen = nullptr;
  for (const json_value &lv : levels.items) {
    if (level == nullptr || std::strcmp(lv["identifier"].string_or(""), level) == 0) {
      chosen = &lv;
      break;
    }
  }
  if (chosen == nullptr) {
    NJIN_WARN("level: %s has no level named '%s'", path, level);
    return level_handle{};
  }

  const std::string dir = level_dir_of(path);
  // "Save levels to separate files": the layers live in a .ldtkl beside it.
  json_value external;
  const json_value *lv = chosen;
  if (!(*chosen)["layerInstances"].is(json_value::array)) {
    const char *rel = (*chosen)["externalRelPath"].string_or(nullptr);
    if (rel == nullptr || !read_json(dir + rel, external)) {
      NJIN_WARN("level: %s: level '%s' has no layers", path, (*chosen)["identifier"].string_or("?"));
      return level_handle{};
    }
    lv = &external;
  }

  const level_handle handle = level_begin(ctx);
  level_slot &slot = *level_slot_of(ctx, handle);
  slot.origin = desc.origin;
  if (desc.use_world_position)
    slot.origin += vec2{(*lv)["worldX"].f32_or(0.0f), (*lv)["worldY"].f32_or(0.0f)};
  slot.size = {(*lv)["pxWid"].f32_or(0.0f), (*lv)["pxHei"].f32_or(0.0f)};
  slot.props = fields_object((*lv)["fieldInstances"]);

  level_builder b{.ctx = ctx, .desc = desc, .handle = handle, .slot = slot, .dir = dir, .loaded = {}};
  ldtk_loader loader{.b = b, .where = path, .tilesets = {}, .project_defs = project["defs"]};
  loader.load_tilesets(project);
  // LDtk lists layers top first; build bottom first so draw layers rise.
  const json_value &layers = (*lv)["layerInstances"];
  for (usize i = layers.size(); i-- > 0;)
    loader.layer(layers.items[i], slot.origin);
  NJIN_INFO("level: loaded %s, level '%s' (%zu entities)", path,
            (*lv)["identifier"].string_or("?"), slot.entities.size());
  return handle;
}
} // namespace njin
