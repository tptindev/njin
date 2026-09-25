#include "njin_level_impl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_file.h"
#include "njin_log.h"
#include "njin_path.h"
#include "njin_prefab_impl.h"
#include "njin_scene.h"
#include "njin_xml.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <raylib.h>

namespace njin {
namespace {
// Tiled keeps flip flags in the top bits of a global tile id.
constexpr u32 gid_flip_h = 0x80000000u;
constexpr u32 gid_flip_v = 0x40000000u;
constexpr u32 gid_flip_d = 0x20000000u;
constexpr u32 gid_rotate_hex = 0x10000000u;
constexpr u32 gid_mask = ~(gid_flip_h | gid_flip_v | gid_flip_d | gid_rotate_hex);

std::string lower(std::string s) {
  for (char &c : s)
    c = (char)std::tolower((unsigned char)c);
  return s;
}

bool ends_with(const std::string &s, const char *suffix) {
  const usize n = std::strlen(suffix);
  return s.size() >= n && lower(s.substr(s.size() - n)) == suffix;
}

// "#AARRGGBB" or "#RRGGBB" (Tiled colours) to rgba.
rgba parse_color(const char *text, rgba fallback) {
  if (text == nullptr || *text == '\0')
    return fallback;
  if (*text == '#')
    text++;
  const usize len = std::strlen(text);
  if (len != 6 && len != 8)
    return fallback;
  const u32 v = (u32)std::strtoul(text, nullptr, 16);
  const f32 a = len == 8 ? (f32)((v >> 24) & 0xFF) / 255.0f : 1.0f;
  return rgba{(f32)((v >> 16) & 0xFF) / 255.0f, (f32)((v >> 8) & 0xFF) / 255.0f,
              (f32)(v & 0xFF) / 255.0f, a};
}

// Parses gids written as CSV (TMX) into `out`.
void parse_csv(std::string_view text, std::vector<u32> &out) {
  usize i = 0;
  while (i < text.size()) {
    while (i < text.size() && !std::isdigit((unsigned char)text[i]))
      i++;
    if (i >= text.size())
      break;
    u64 v = 0;
    while (i < text.size() && std::isdigit((unsigned char)text[i]))
      v = v * 10 + (u64)(text[i++] - '0');
    out.push_back((u32)v);
  }
}

// Decodes base64 + optional compression into little-endian gids.
bool decode_gids(std::string_view text, const char *compression, std::vector<u32> &out,
                 const char *where) {
  std::vector<u8> raw;
  std::vector<u8> bytes;
  if (!level_base64(text, raw)) {
    NJIN_WARN("level: %s: bad base64 tile data", where);
    return false;
  }
  if (compression != nullptr && std::strcmp(compression, "zstd") == 0) {
    NJIN_WARN("level: %s: zstd compression is not supported; save the map with zlib, "
              "gzip or CSV", where);
    return false;
  }
  if (!level_decompress(raw, compression != nullptr ? compression : "", bytes)) {
    NJIN_WARN("level: %s: cannot decompress tile data (%s)", where, compression);
    return false;
  }
  for (usize i = 0; i + 3 < bytes.size(); i += 4)
    out.push_back((u32)bytes[i] | ((u32)bytes[i + 1] << 8) | ((u32)bytes[i + 2] << 16) |
                  ((u32)bytes[i + 3] << 24));
  return true;
}

// --- TMX / TSX (XML) to the JSON shape of TMJ / TSJ ---

json_value xml_props(const xml_node &node) {
  json_value list = json_value::make_array();
  const xml_node *props = node.child("properties");
  if (props == nullptr)
    return list;
  for (const xml_node &p : props->children) {
    if (p.name != "property")
      continue;
    const std::string type = p.attr("type", "string");
    const char *text = p.attr("value", p.text.c_str());
    json_value value;
    if (type == "bool")
      value = json_value(std::strcmp(text, "true") == 0);
    else if (type == "int" || type == "float" || type == "object")
      value = json_value(std::strtod(text, nullptr));
    else if (type == "class") {
      value = json_value::make_object();
      for (const json_value &inner : xml_props(p).items)
        value.set(inner["name"].string_or(""), inner["value"]);
    } else
      value = json_value(text);
    list.push(json_value::make_object()
                  .set("name", p.attr("name", ""))
                  .set("type", type)
                  .set("value", std::move(value)));
  }
  return list;
}

void copy_numbers(const xml_node &node, json_value &out, std::initializer_list<const char *> keys) {
  for (const char *k : keys) {
    if (node.attr(k) != nullptr)
      out.set(k, node.attr_number(k, 0.0));
  }
}

void copy_strings(const xml_node &node, json_value &out, std::initializer_list<const char *> keys) {
  for (const char *k : keys) {
    if (const char *v = node.attr(k))
      out.set(k, v);
  }
}

json_value xml_tileset(const xml_node &ts) {
  json_value out = json_value::make_object();
  copy_numbers(ts, out, {"firstgid", "tilewidth", "tileheight", "spacing", "margin",
                         "tilecount", "columns"});
  copy_strings(ts, out, {"source", "name"});
  if (const xml_node *img = ts.child("image")) {
    out.set("image", img->attr("source", ""));
    out.set("imagewidth", img->attr_number("width", 0.0));
    out.set("imageheight", img->attr_number("height", 0.0));
  }
  for (const xml_node &t : ts.children) {
    if (t.name == "tile" && t.child("image") != nullptr) {
      out.set("tiles", json_value::make_array().push(json_value::make_object()));
      break;
    }
  }
  return out;
}

// Reads a <data> element into a flat gid array or chunk list.
void xml_data(const xml_node &data, json_value &layer, const char *where) {
  const char *encoding = data.attr("encoding", "");
  const char *compression = data.attr("compression", "");
  const auto read = [&](const xml_node &node, json_value &target) {
    std::vector<u32> gids;
    if (std::strcmp(encoding, "csv") == 0) {
      parse_csv(node.text, gids);
    } else if (std::strcmp(encoding, "base64") == 0) {
      decode_gids(node.text, compression, gids, where);
    } else {
      for (const xml_node &t : node.children) // deprecated <tile gid=""/> form
        if (t.name == "tile")
          gids.push_back((u32)t.attr_number("gid", 0.0));
    }
    json_value arr = json_value::make_array();
    arr.items.reserve(gids.size());
    for (const u32 g : gids)
      arr.push(json_value((f64)g));
    target.set("data", std::move(arr));
  };
  bool chunked = false;
  json_value chunks = json_value::make_array();
  for (const xml_node &c : data.children) {
    if (c.name != "chunk")
      continue;
    chunked = true;
    json_value chunk = json_value::make_object();
    copy_numbers(c, chunk, {"x", "y", "width", "height"});
    read(c, chunk);
    chunks.push(std::move(chunk));
  }
  if (chunked)
    layer.set("chunks", std::move(chunks));
  else
    read(data, layer);
}

json_value xml_layers(const xml_node &parent, const char *where) {
  json_value layers = json_value::make_array();
  for (const xml_node &n : parent.children) {
    json_value layer = json_value::make_object();
    copy_strings(n, layer, {"name", "class", "tintcolor"});
    copy_numbers(n, layer, {"x", "y", "width", "height", "opacity", "offsetx", "offsety"});
    layer.set("visible", n.attr_number("visible", 1.0) != 0.0);
    layer.set("properties", xml_props(n));
    if (n.name == "layer") {
      layer.set("type", "tilelayer");
      if (const xml_node *data = n.child("data"))
        xml_data(*data, layer, where);
    } else if (n.name == "objectgroup") {
      layer.set("type", "objectgroup");
      json_value objects = json_value::make_array();
      for (const xml_node &o : n.children) {
        if (o.name != "object")
          continue;
        json_value obj = json_value::make_object();
        copy_numbers(o, obj, {"id", "x", "y", "width", "height", "rotation", "gid"});
        copy_strings(o, obj, {"name", "type", "class", "template"});
        obj.set("visible", o.attr_number("visible", 1.0) != 0.0);
        obj.set("properties", xml_props(o));
        if (o.child("ellipse") != nullptr)
          obj.set("ellipse", true);
        if (o.child("point") != nullptr)
          obj.set("point", true);
        for (const char *shape : {"polygon", "polyline"}) {
          const xml_node *poly = o.child(shape);
          if (poly == nullptr)
            continue;
          json_value pts = json_value::make_array();
          std::string_view s = poly->attr("points", "");
          while (!s.empty()) {
            const usize space = s.find(' ');
            const std::string pair(s.substr(0, space));
            s = space == std::string_view::npos ? std::string_view{} : s.substr(space + 1);
            const usize comma = pair.find(',');
            if (comma == std::string::npos)
              continue;
            pts.push(json_value::make_object()
                         .set("x", std::strtod(pair.c_str(), nullptr))
                         .set("y", std::strtod(pair.c_str() + comma + 1, nullptr)));
          }
          obj.set(shape, std::move(pts));
        }
        objects.push(std::move(obj));
      }
      layer.set("objects", std::move(objects));
    } else if (n.name == "group") {
      layer.set("type", "group");
      layer.set("layers", xml_layers(n, where));
    } else if (n.name == "imagelayer") {
      layer.set("type", "imagelayer");
      if (const xml_node *img = n.child("image"))
        layer.set("image", img->attr("source", ""));
    } else {
      continue;
    }
    layers.push(std::move(layer));
  }
  return layers;
}

json_value xml_map(const xml_node &map, const char *where) {
  json_value out = json_value::make_object();
  copy_strings(map, out, {"orientation", "class"});
  copy_numbers(map, out, {"width", "height", "tilewidth", "tileheight"});
  out.set("infinite", map.attr_number("infinite", 0.0) != 0.0);
  out.set("properties", xml_props(map));
  json_value tilesets = json_value::make_array();
  for (const xml_node &n : map.children)
    if (n.name == "tileset")
      tilesets.push(xml_tileset(n));
  out.set("tilesets", std::move(tilesets));
  out.set("layers", xml_layers(map, where));
  return out;
}

// Reads a .tmx/.tmj/.tsx/.tsj into the TMJ/TSJ JSON shape.
bool read_tiled(const std::string &path, json_value &out) {
  std::string text;
  if (!file_read(path.c_str(), text)) {
    NJIN_WARN("level: cannot read %s", path.c_str());
    return false;
  }
  std::string error;
  const bool is_xml = !text.empty() && text.find('<') < text.find('{');
  if (!is_xml) {
    if (!json_parse(text, out, &error)) {
      NJIN_WARN("level: %s: %s", path.c_str(), error.c_str());
      return false;
    }
    return true;
  }
  xml_node root;
  if (!xml_parse(text, root, error)) {
    NJIN_WARN("level: %s: %s", path.c_str(), error.c_str());
    return false;
  }
  if (root.name == "map")
    out = xml_map(root, path.c_str());
  else if (root.name == "tileset")
    out = xml_tileset(root);
  else {
    NJIN_WARN("level: %s: not a Tiled map or tileset (<%s>)", path.c_str(), root.name.c_str());
    return false;
  }
  return true;
}

// Tiled properties (a list of {name, type, value}) as one object.
json_value props_object(const json_value &list) {
  json_value out = json_value::make_object();
  for (const json_value &p : list.items)
    out.set(p["name"].string_or(""), p["value"]);
  return out;
}

// --- Tiled map building ---

struct tiled_tileset {
  u32 firstgid = 1;
  level_tileset tiles;
  bool usable = false;
};

struct tiled_loader {
  level_builder &b;
  const std::string where;
  vec2 grid{};
  std::vector<tiled_tileset> tilesets;

  // Tileset holding global id `id` (flags removed), or nullptr.
  const tiled_tileset *tileset_of(u32 id, usize *index) const {
    for (usize i = tilesets.size(); i-- > 0;) {
      if (tilesets[i].firstgid <= id) {
        *index = i;
        return &tilesets[i];
      }
    }
    return nullptr;
  }

  void load_tilesets(const json_value &map) {
    for (const json_value &entry : map["tilesets"].items) {
      tiled_tileset ts;
      ts.firstgid = (u32)entry["firstgid"].number_or(1.0);
      json_value external;
      const json_value *def = &entry;
      std::string base = b.dir;
      if (const char *source = entry["source"].string_or(nullptr)) {
        const std::string file = b.dir + source;
        if (!read_tiled(file, external)) {
          tilesets.push_back(ts);
          continue;
        }
        def = &external;
        base = level_dir_of(file);
      }
      if ((*def)["tiles"].size() > 0 && (*def)["image"].is(json_value::null)) {
        NJIN_WARN("level: %s: tileset '%s' is an image collection, which is not supported",
                  where.c_str(), (*def)["name"].string_or("?"));
        tilesets.push_back(ts);
        continue;
      }
      const char *image = (*def)["image"].string_or(nullptr);
      if (image == nullptr) {
        tilesets.push_back(ts);
        continue;
      }
      ts.tiles.texture = b.texture(image, base);
      ts.tiles.tile_size = {(*def)["tilewidth"].f32_or(grid.x), (*def)["tileheight"].f32_or(grid.y)};
      ts.tiles.margin = (*def)["margin"].f32_or(0.0f);
      ts.tiles.spacing = (*def)["spacing"].f32_or(0.0f);
      ts.tiles.columns = (*def)["columns"].int_or(0);
      if (ts.tiles.columns <= 0) {
        const f32 w = (*def)["imagewidth"].f32_or(0.0f);
        ts.tiles.columns = (i32)((w - 2.0f * ts.tiles.margin + ts.tiles.spacing) /
                                 (ts.tiles.tile_size.x + ts.tiles.spacing));
      }
      if (ts.tiles.tile_size.x != grid.x || ts.tiles.tile_size.y != grid.y)
        NJIN_WARN("level: %s: tileset '%s' has %gx%g tiles on a %gx%g grid; they are "
                  "drawn at their own size", where.c_str(), (*def)["name"].string_or("?"),
                  ts.tiles.tile_size.x, ts.tiles.tile_size.y, grid.x, grid.y);
      ts.usable = ts.tiles.texture.id != 0;
      tilesets.push_back(ts);
    }
    std::sort(tilesets.begin(), tilesets.end(),
              [](const tiled_tileset &a, const tiled_tileset &c) { return a.firstgid < c.firstgid; });
  }

  // Converts a gid to a tilemap value, or -1.
  i32 tile_value(u32 gid, usize *tileset_index) {
    const u32 id = gid & gid_mask;
    if (id == 0)
      return -1;
    const tiled_tileset *ts = tileset_of(id, tileset_index);
    if (ts == nullptr || !ts->usable)
      return -1;
    if ((gid & gid_flip_d) != 0 && !b.warned_flip_diagonal) {
      b.warned_flip_diagonal = true;
      NJIN_WARN("level: %s: rotated (diagonally flipped) tiles are drawn unrotated", where.c_str());
    }
    i32 value = (i32)(id - ts->firstgid);
    if ((gid & gid_flip_h) != 0)
      value |= tile_flip_x;
    if ((gid & gid_flip_v) != 0)
      value |= tile_flip_y;
    return value;
  }

  // Reads one layer's gids (array, or base64 string) starting at cell (x0, y0).
  void read_gids(const json_value &holder, const json_value &layer, i32 x0, i32 y0, i32 width,
                 std::vector<std::vector<std::pair<cell, i32>>> &per_tileset) {
    std::vector<u32> gids;
    const json_value &data = holder["data"];
    if (data.is(json_value::array)) {
      gids.reserve(data.items.size());
      for (const json_value &g : data.items)
        gids.push_back((u32)g.number_or(0.0));
    } else if (data.is(json_value::string)) {
      decode_gids(data.str, layer["compression"].string_or(""), gids, where.c_str());
    }
    if (width <= 0)
      return;
    for (usize i = 0; i < gids.size(); i++) {
      usize ts = 0;
      const i32 value = tile_value(gids[i], &ts);
      if (value < 0)
        continue;
      per_tileset[ts].push_back({cell{x0 + (i32)i % width, y0 + (i32)i / width}, value});
    }
  }

  void tile_layer(const json_value &layer, vec2 offset, f32 opacity, bool visible) {
    std::vector<std::vector<std::pair<cell, i32>>> per_tileset(tilesets.size());
    if (layer["chunks"].is(json_value::array)) {
      for (const json_value &chunk : layer["chunks"].items)
        read_gids(chunk, layer, chunk["x"].int_or(0), chunk["y"].int_or(0),
                  chunk["width"].int_or(0), per_tileset);
    } else {
      read_gids(layer, layer, layer["x"].int_or(0), layer["y"].int_or(0),
                layer["width"].int_or(0), per_tileset);
    }
    level_tile_layer info;
    info.name = layer["name"].string_or("");
    info.origin = b.slot.origin + offset;
    info.visible = visible;
    info.solid = b.is_solid(info.name, props_object(layer["properties"])["solid"].bool_or(false));
    info.tint = parse_color(layer["tintcolor"].string_or(nullptr), info.tint);
    info.tint.a *= opacity;
    info.draw_layer = b.take_draw_layer();
    for (usize i = 0; i < tilesets.size(); i++) {
      if (!per_tileset[i].empty())
        b.add_tiles(info, tilesets[i].tiles, per_tileset[i]);
    }
  }

  void object_layer(const json_value &layer, vec2 offset, bool visible) {
    const std::string name = layer["name"].string_or("");
    const bool solid = b.is_solid(name, props_object(layer["properties"])["solid"].bool_or(false));
    const i32 draw_layer = b.take_draw_layer();
    for (const json_value &o : layer["objects"].items) {
      if (o["template"].is(json_value::string))
        NJIN_WARN("level: %s: object templates (%s) are not expanded", where.c_str(),
                  o["template"].str.c_str());
      level_object obj;
      obj.name = o["name"].string_or("");
      obj.type = o["type"].string_or("");
      if (obj.type.empty())
        obj.type = o["class"].string_or("");
      obj.size = {o["width"].f32_or(0.0f), o["height"].f32_or(0.0f)};
      obj.props = props_object(o["properties"]);
      obj.ellipse = o["ellipse"].bool_or(false);
      const vec2 corner = b.slot.origin + offset + vec2{o["x"].f32_or(0.0f), o["y"].f32_or(0.0f)};
      const f32 rot = o["rotation"].f32_or(0.0f);
      transform at{.pos = corner, .rot = rot};

      sprite tile_sprite{};
      bool has_sprite = false;
      if (const u32 gid = (u32)o["gid"].number_or(0.0); gid != 0) {
        // Tile objects sit on their bottom-left corner.
        at.pos = corner + rotate(vec2{obj.size.x * 0.5f, -obj.size.y * 0.5f}, rot);
        usize ts = 0;
        const i32 value = tile_value(gid, &ts);
        if (value >= 0) {
          const level_tileset &set = tilesets[ts].tiles;
          tile_sprite.texture = set.texture;
          tile_sprite.source = set.source(tile_id(value));
          tile_sprite.flip_x = (value & tile_flip_x) != 0;
          tile_sprite.flip_y = (value & tile_flip_y) != 0;
          tile_sprite.layer = draw_layer;
          tile_sprite.visible = visible && o["visible"].bool_or(true);
          if (set.tile_size.x > 0.0f && obj.size.x > 0.0f)
            at.scale = obj.size.x / set.tile_size.x;
          has_sprite = true;
        }
      } else if (o["point"].bool_or(false)) {
        at.pos = corner;
      } else if (o["polygon"].is(json_value::array) || o["polyline"].is(json_value::array)) {
        const bool closed = o["polygon"].is(json_value::array);
        obj.closed = closed;
        for (const json_value &p : o[closed ? "polygon" : "polyline"].items)
          obj.points.push_back({p["x"].f32_or(0.0f), p["y"].f32_or(0.0f)});
        at.pos = corner; // points are relative to the object's position
      } else {
        at.pos = corner + rotate(obj.size * 0.5f, rot);
      }
      b.add_object(std::move(obj), at, solid, has_sprite ? &tile_sprite : nullptr);
    }
  }

  void image_layer(const json_value &layer, vec2 offset, f32 opacity, bool visible) {
    const i32 draw_layer = b.take_draw_layer();
    const char *image = layer["image"].string_or(nullptr);
    if (image == nullptr || *image == '\0')
      return;
    entt::registry &reg = world(b.ctx);
    const entt::entity e = reg.create();
    reg.emplace<transform>(e, transform{.pos = b.slot.origin + offset});
    reg.emplace<sprite>(e, sprite{.texture = b.texture(image),
                                  .origin = {0.0f, 0.0f},
                                  .tint = {1.0f, 1.0f, 1.0f, opacity},
                                  .layer = draw_layer,
                                  .visible = visible});
    b.own(e);
  }

  void layers(const json_value &list, vec2 offset, f32 opacity, bool visible) {
    for (const json_value &layer : list.items) {
      const vec2 off = offset + vec2{layer["offsetx"].f32_or(0.0f), layer["offsety"].f32_or(0.0f)};
      const f32 op = opacity * layer["opacity"].f32_or(1.0f);
      const bool vis = visible && layer["visible"].bool_or(true);
      const std::string type = layer["type"].string_or("");
      if (type == "tilelayer")
        tile_layer(layer, off, op, vis);
      else if (type == "objectgroup")
        object_layer(layer, off, vis);
      else if (type == "imagelayer")
        image_layer(layer, off, op, vis);
      else if (type == "group")
        layers(layer["layers"], off, op, vis);
    }
  }
};

level_handle load_tiled(njin_ctx &ctx, const std::string &path, const level_desc &desc) {
  json_value map;
  if (!read_tiled(path, map))
    return level_handle{};
  if (!map["layers"].is(json_value::array) || !map["tilewidth"].is(json_value::number)) {
    NJIN_WARN("level: %s is not a Tiled map", path.c_str());
    return level_handle{};
  }
  // njin makes top-down and platformer games, which sit on a square grid.
  // Drawing any other grid as squares would only produce a wrong level.
  const char *orientation = map["orientation"].string_or("orthogonal");
  if (std::strcmp(orientation, "orthogonal") != 0) {
    NJIN_WARN("level: %s has %s orientation; only orthogonal (square grid) maps are "
              "supported", path.c_str(), orientation);
    return level_handle{};
  }

  const level_handle handle = level_begin(ctx);
  level_slot &slot = *level_slot_of(ctx, handle);
  slot.origin = desc.origin;
  slot.props = props_object(map["properties"]);
  const vec2 grid{map["tilewidth"].f32_or(16.0f), map["tileheight"].f32_or(16.0f)};
  slot.size = {map["width"].f32_or(0.0f) * grid.x, map["height"].f32_or(0.0f) * grid.y};

  level_builder b{.ctx = ctx, .desc = desc, .handle = handle, .slot = slot,
                  .dir = level_dir_of(path), .loaded = {}};
  tiled_loader loader{.b = b, .where = path, .grid = grid, .tilesets = {}};
  loader.load_tilesets(map);
  loader.layers(map["layers"], {}, 1.0f, true);
  return handle;
}
} // namespace

// --- shared helpers ---

std::string level_dir_of(const std::string &path) {
  const usize slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

bool level_base64(std::string_view text, std::vector<u8> &out) {
  u32 acc = 0;
  i32 bits = 0;
  for (const char c : text) {
    i32 v = -1;
    if (c >= 'A' && c <= 'Z')
      v = c - 'A';
    else if (c >= 'a' && c <= 'z')
      v = c - 'a' + 26;
    else if (c >= '0' && c <= '9')
      v = c - '0' + 52;
    else if (c == '+')
      v = 62;
    else if (c == '/')
      v = 63;
    else if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
      continue;
    else
      return false;
    acc = (acc << 6) | (u32)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((u8)((acc >> bits) & 0xFF));
    }
  }
  return true;
}

bool level_decompress(const std::vector<u8> &in, const char *method, std::vector<u8> &out) {
  usize start = 0;
  if (std::strcmp(method, "zlib") == 0) {
    start = 2; // CMF, FLG; the deflate stream follows
  } else if (std::strcmp(method, "gzip") == 0) {
    if (in.size() < 10 || in[0] != 0x1f || in[1] != 0x8b)
      return false;
    const u8 flags = in[3];
    start = 10;
    if ((flags & 4) != 0 && start + 2 <= in.size())
      start += 2 + ((usize)in[start] | ((usize)in[start + 1] << 8)); // FEXTRA
    if ((flags & 8) != 0) // FNAME
      while (start < in.size() && in[start++] != 0) {}
    if ((flags & 16) != 0) // FCOMMENT
      while (start < in.size() && in[start++] != 0) {}
    if ((flags & 2) != 0) // FHCRC
      start += 2;
  } else {
    out = in;
    return true;
  }
  if (start >= in.size())
    return false;
  i32 size = 0;
  unsigned char *data = DecompressData(in.data() + start, (i32)(in.size() - start), &size);
  if (data == nullptr || size <= 0) {
    if (data != nullptr)
      MemFree(data);
    return false;
  }
  out.assign(data, data + size);
  MemFree(data);
  return true;
}

texture_handle level_builder::texture(const std::string &relative, const std::string &base) {
  const std::string path = (base.empty() ? dir : base) + relative;
  if (const auto it = loaded.find(path); it != loaded.end())
    return it->second;
  const texture_handle tex = texture_store_load(ctx.texture, path.c_str());
  if (tex.id != 0) {
    texture_store_set_filter(ctx.texture, tex, desc.filter);
    slot.textures.push_back(tex);
  }
  loaded[path] = tex;
  return tex;
}

bool level_builder::is_solid(const std::string &name, bool solid_property) const {
  if (!desc.solid_layers.empty())
    return std::find(desc.solid_layers.begin(), desc.solid_layers.end(), name) !=
           desc.solid_layers.end();
  if (solid_property)
    return true;
  const std::string n = lower(name);
  for (const char *word : {"collision", "collide", "solid", "wall"})
    if (n.find(word) != std::string::npos)
      return true;
  return false;
}

void level_builder::own(entt::entity entity) {
  if (desc.scene_owned && slot.scene.id != 0)
    world(ctx).emplace_or_replace<scene_owned>(entity, scene_owned{slot.scene});
  slot.entities.push_back(entity);
}

void level_builder::add_tiles(const level_tile_layer &layer, const level_tileset &tileset,
                              const std::vector<std::pair<cell, i32>> &values) {
  entt::registry &reg = world(ctx);
  // Stacked cells spill into further tilemaps, each on its own draw layer
  // just above, in the order the tiles came.
  std::vector<entt::entity> stack;
  for (const auto &[c, value] : values) {
    usize level = 0;
    while (level < stack.size() && tilemap_get(reg.get<tilemap>(stack[level]), c.x, c.y) >= 0)
      level++;
    if (level == stack.size()) {
      const entt::entity e = reg.create();
      reg.emplace<transform>(e, transform{.pos = layer.origin});
      tilemap map{};
      map.tileset = tileset.texture;
      map.tile_size = tileset.tile_size;
      map.margin = tileset.margin;
      map.spacing = tileset.spacing;
      map.layer = level == 0 ? layer.draw_layer : take_draw_layer();
      map.tint = layer.tint;
      map.visible = layer.visible;
      reg.emplace<tilemap>(e, std::move(map));
      if (layer.solid) {
        collider col = desc.solid;
        col.shape = collider_tiles;
        reg.emplace<collider>(e, col);
      }
      own(e);
      stack.push_back(e);
    }
    tilemap_set(reg.get<tilemap>(stack[level]), c.x, c.y, value);
  }
}

void level_builder::add_value_grid(const level_tile_layer &layer, vec2 cell_size,
                                   const std::vector<std::pair<cell, i32>> &values) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = layer.origin});
  tilemap map{};
  map.tile_size = cell_size;
  map.layer = layer.draw_layer;
  map.visible = false;
  for (const auto &[c, value] : values)
    tilemap_set(map, c.x, c.y, value);
  reg.emplace<tilemap>(e, std::move(map));
  if (layer.solid) {
    collider col = desc.solid;
    col.shape = collider_tiles;
    reg.emplace<collider>(e, col);
  }
  own(e);
}

namespace {
struct prepare_data {
  level_object *object;
};

void attach_object(njin_ctx &ctx, entt::entity entity, void *user) {
  world(ctx).emplace<level_object>(entity, std::move(*static_cast<prepare_data *>(user)->object));
}
} // namespace

entt::entity level_builder::add_object(level_object object, const transform &at, bool solid,
                                       const sprite *tile_sprite) {
  entt::registry &reg = world(ctx);
  object.level = handle;
  const prefab_handle prefab = object.type.empty() ? prefab_handle{} : prefab_find(ctx, object.type.c_str());
  if (prefab.id != 0) {
    prepare_data data{&object};
    const entt::entity e = prefab_spawn_prepared(ctx, prefab, at, attach_object, &data);
    if (e != entt::null) {
      if (tile_sprite != nullptr && !reg.all_of<sprite>(e))
        reg.emplace<sprite>(e, *tile_sprite);
      own(e);
    }
    return e;
  }
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, at);
  if (tile_sprite != nullptr)
    reg.emplace<sprite>(e, *tile_sprite);
  if (solid && object.points.empty() && object.size.x > 0.0f && object.size.y > 0.0f) {
    collider col = desc.solid;
    if (object.ellipse) {
      col.shape = collider_circle;
      col.radius = std::max(object.size.x, object.size.y) * 0.5f;
    } else {
      col.shape = collider_box;
      col.size = object.size;
    }
    reg.emplace<collider>(e, col);
  } else if (solid && !object.points.empty()) {
    NJIN_WARN("level: polygon object '%s' on a solid layer has no collider "
              "(only rectangles and ellipses do)", object.name.c_str());
  }
  reg.emplace<level_object>(e, std::move(object));
  own(e);
  return e;
}

level_handle level_begin(njin_ctx &ctx) {
  level_slot slot{};
  slot.alive = true;
  slot.scene = scene_current(ctx);
  ctx.level.levels.push_back(std::move(slot));
  return level_handle{.id = (u32)ctx.level.levels.size()};
}

level_slot *level_slot_of(njin_ctx &ctx, level_handle level) {
  if (level.id == 0 || level.id > ctx.level.levels.size())
    return nullptr;
  level_slot &slot = ctx.level.levels[level.id - 1];
  return slot.alive ? &slot : nullptr;
}

namespace {
const level_slot *slot_of(const njin_ctx &ctx, level_handle level) {
  return level_slot_of(const_cast<njin_ctx &>(ctx), level);
}
} // namespace

void level_abort(njin_ctx &ctx, level_handle level) { level_unload(ctx, level); }

void level_unload(njin_ctx &ctx, level_handle level) {
  level_slot *slot = level_slot_of(ctx, level);
  if (slot == nullptr)
    return;
  entt::registry &reg = world(ctx);
  for (const entt::entity e : slot->entities) {
    if (reg.valid(e))
      reg.destroy(e);
  }
  for (const texture_handle tex : slot->textures)
    texture_store_unload(ctx.texture, tex);
  *slot = level_slot{};
}

void level_store_scene_exit(njin_ctx &ctx, scene_handle scene) {
  for (usize i = 0; i < ctx.level.levels.size(); i++) {
    const level_slot &slot = ctx.level.levels[i];
    if (slot.alive && slot.scene.id == scene.id && scene.id != 0)
      level_unload(ctx, level_handle{.id = (u32)(i + 1)});
  }
}

level_handle level_load(njin_ctx &ctx, const char *path, const level_desc &desc) {
  if (path == nullptr) {
    NJIN_WARN("level_load: path is null");
    return level_handle{};
  }
  const std::string resolved = asset_path(path);
  if (!file_exists(resolved.c_str())) {
    NJIN_WARN("level_load: file not found: %s", path);
    return level_handle{};
  }
  if (ends_with(resolved, ".ldtk"))
    return level_load_ldtk_file(ctx, resolved.c_str(), nullptr, desc);
  const level_handle handle = load_tiled(ctx, resolved, desc);
  if (handle.id != 0)
    NJIN_INFO("level: loaded %s (%zu entities)", path,
              ctx.level.levels[handle.id - 1].entities.size());
  return handle;
}

level_handle level_load_ldtk(njin_ctx &ctx, const char *path, const char *level,
                             const level_desc &desc) {
  if (path == nullptr) {
    NJIN_WARN("level_load_ldtk: path is null");
    return level_handle{};
  }
  return level_load_ldtk_file(ctx, asset_path(path).c_str(), level, desc);
}

vec2 level_size(const njin_ctx &ctx, level_handle level) {
  const level_slot *slot = slot_of(ctx, level);
  return slot != nullptr ? slot->size : vec2{};
}

vec2 level_origin(const njin_ctx &ctx, level_handle level) {
  const level_slot *slot = slot_of(ctx, level);
  return slot != nullptr ? slot->origin : vec2{};
}

const json_value &level_properties(const njin_ctx &ctx, level_handle level) {
  static const json_value none{};
  const level_slot *slot = slot_of(ctx, level);
  return slot != nullptr ? slot->props : none;
}

entt::entity level_find(njin_ctx &ctx, level_handle level, const char *name) {
  const level_slot *slot = level_slot_of(ctx, level);
  if (slot == nullptr || name == nullptr)
    return entt::null;
  entt::registry &reg = world(ctx);
  entt::entity by_type = entt::null;
  for (const entt::entity e : slot->entities) {
    const level_object *obj = reg.valid(e) ? reg.try_get<level_object>(e) : nullptr;
    if (obj == nullptr)
      continue;
    if (obj->name == name)
      return e;
    if (by_type == entt::null && obj->type == name)
      by_type = e;
  }
  return by_type;
}
} // namespace njin
