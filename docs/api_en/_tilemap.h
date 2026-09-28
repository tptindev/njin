#pragma once
#include "_collide.h"
#include <array>
#include <cmath>
#include <initializer_list>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace njin {
/// @addtogroup grp_tilemap
/// @{

/// Number of tiles per side of a chunk. A chunk has 32 x 32 tiles.
inline constexpr i32 tile_chunk_size = 32;

/// Bit marking a horizontally flipped tile, added to the tile index: `id | tile_flip_x`.
inline constexpr i32 tile_flip_x = 1 << 29;
/// Bit marking a vertically flipped tile, added to the tile index: `id | tile_flip_y`.
inline constexpr i32 tile_flip_y = 1 << 30;

/// Tile index in the tileset, without the flip bits.
/// @param value Tile value, from tilemap_get().
/// @return The tile index, or -1 if the tile is empty.
constexpr i32 tile_id(i32 value) {
  return value < 0 ? -1 : value & ~(tile_flip_x | tile_flip_y);
}

/// A 32 x 32 block of tiles of a tilemap.
///
/// A tilemap only keeps chunks that have at least one non-empty tile, so a wide
/// but sparse map stays light, and tile coordinates can be negative.
struct tile_chunk {
  /// Tiles by row, -1 is an empty tile.
  std::array<i32, (usize)tile_chunk_size * tile_chunk_size> tiles;
  i32 filled = 0;  ///< Number of non-empty tiles. A chunk that drops to 0 is removed.
  u32 version = 0; ///< Incremented each time the chunk changes, so the engine knows to redraw.

  tile_chunk() { tiles.fill(-1); }
};

/// Position of a tile in the grid. Can be negative.
struct cell {
  i32 x = 0; ///< Column.
  i32 y = 0; ///< Row.
};

/// Collision shape of a tile type, used by collision_move() and collision_raycast()
/// with `collider_tiles`. Set with tilemap_set_shape(); a tile that is not set is `tile_solid`.
///
/// Slopes are for platformer games: a slope surface only supports from above, and the
/// bottom side of the tile is still an obstacle. The `_r` name is a slope **high on the
/// right** (moving right goes uphill), `_l` is high on the left. A 22.5 degree slope
/// takes two adjacent tiles: `_low` (lower half) then `_high` (upper half).
enum tile_shape : u8 {
  tile_solid = 0,    ///< Blocks every side. Default.
  tile_none,         ///< No collision: visual only, even when in an obstacle layer.
  tile_one_way,      ///< One-way platform: only supports from above, can be jumped through from below.
  tile_slope_r,      ///< 45 degree slope, low on the left, high on the right.
  tile_slope_l,      ///< 45 degree slope, high on the left, low on the right.
  tile_slope_r_low,  ///< 22.5 degree slope high on the right, lower half (0 to half a tile).
  tile_slope_r_high, ///< 22.5 degree slope high on the right, upper half (half a tile to a full tile).
  tile_slope_l_low,  ///< 22.5 degree slope high on the left, lower half.
  tile_slope_l_high, ///< 22.5 degree slope high on the left, upper half.
  tile_shape_count   ///< Number of shapes. Not a real shape.
};

/// Whether a tile is a slope.
/// @param shape Shape of the tile.
/// @return `true` for the `tile_slope_*` shapes.
constexpr bool tile_is_slope(tile_shape shape) {
  return shape >= tile_slope_r && shape <= tile_slope_l_high;
}

/// Height of the slope surface at a point within the tile, measured from the tile bottom as a fraction of the tile height.
/// @param shape Shape of the tile.
/// @param u Horizontal position within the tile, 0 is the left edge, 1 is the right edge.
/// @return 0 (surface at the tile bottom) to 1 (surface at the tile top). A tile that is not a slope returns 1.
constexpr f32 tile_surface(tile_shape shape, f32 u) {
  u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
  switch (shape) {
  case tile_slope_r: return u;
  case tile_slope_l: return 1.0f - u;
  case tile_slope_r_low: return u * 0.5f;
  case tile_slope_r_high: return 0.5f + u * 0.5f;
  case tile_slope_l_low: return (1.0f - u) * 0.5f;
  case tile_slope_l_high: return 0.5f + (1.0f - u) * 0.5f;
  default: return 1.0f;
  }
}

/// Parse the name of a tile shape, as in Tiled properties or LDtk IntGrid
/// value names: `solid`, `none` (or `empty`), `one_way` (or `oneway`,
/// `platform`), `slope_r`, `slope_l`, `slope_r_low`, `slope_r_high`,
/// `slope_l_low`, `slope_l_high`. Case-insensitive; `-` and spaces
/// are treated as `_`.
/// @param name Name.
/// @param out Receives the shape if the name is valid.
/// @return `true` if the name was recognized.
bool tile_shape_from_name(const char *name, tile_shape &out);

/// Animation of a tile type: water, torch, swaying grass. All tiles with the same index in the
/// tilemap run in sync. Created with tilemap_animate(); loaded automatically from Tiled.
struct tile_anim {
  std::vector<i32> frames;    ///< Tile index of each frame in the tileset.
  std::vector<f32> durations; ///< Duration of each frame, seconds.
  f32 total = 0.0f;           ///< Total duration, precomputed.
};

/// A grid of square tiles with no size limit, drawn from a tileset, split into chunks.
///
/// Needs a transform on the same entity: `transform.pos` is the top-left corner of tile
/// (0, 0) in the world. Rotation and scale are ignored.
///
/// The tileset is an image made of tiles of the same size `tile_size`, numbered from 0
/// by row from left to right and then top to bottom, and may be offset from the image edge by `margin`
/// and separated by `spacing` pixels (like Tiled and LDtk tilesets). Each grid tile
/// holds its tile index in the tileset, or -1 for an empty tile. Add njin::tile_flip_x
/// or njin::tile_flip_y to flip a tile when drawing; read the index with tile_id().
///
/// **Chunking.** Tiles are stored in 32 x 32 blocks (njin::tile_chunk). The engine's sprite
/// module pre-draws each chunk into its own image and only redraws chunks with a tile that
/// just changed, so a large map costs only a few draw calls per frame. Only chunks inside the
/// camera view are drawn, and the image of a chunk that has been out of view for a while is
/// released.
///
/// Always change tiles with tilemap_set() so the engine knows which chunk needs redrawing.
struct tilemap {
  texture_handle tileset{};     ///< Tileset image.
  vec2 tile_size{16.0f, 16.0f}; ///< Size of one tile, in pixels.
  f32 margin = 0.0f;  ///< Distance from the tileset image edge to the first tile, pixels.
  f32 spacing = 0.0f; ///< Distance between two adjacent tiles in the tileset image, pixels.
  i32 layer = 0;                ///< Draw layer, same scale as sprite::layer.
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Color multiplied into every tile.
  bool visible = true;          ///< Hidden when drawing. Still usable for collision.
  /// Chunks that have tiles, keyed by tile_chunk_key().
  std::unordered_map<u64, tile_chunk> chunks;
  u32 revision = 0; ///< Incremented each time a tile changes.
  /// Collision shape by tile index: `shapes[id]`. A tile outside the list is
  /// `tile_solid`. Set with tilemap_set_shape().
  std::vector<u8> shapes;
  /// Animation by base tile index. Set with tilemap_animate().
  std::unordered_map<i32, tile_anim> anims;
};

/// Set the collision shape for every tile with index `id` in the tilemap.
/// @param map Tilemap.
/// @param id Tile index in the tileset (without the flip bits).
/// @param shape Shape.
inline void tilemap_set_shape(tilemap &map, i32 id, tile_shape shape) {
  if (id < 0)
    return;
  if ((usize)id >= map.shapes.size())
    map.shapes.resize((usize)id + 1, tile_solid);
  map.shapes[(usize)id] = shape;
}

/// Collision shape of a tile.
/// @param map Tilemap.
/// @param value Tile value from tilemap_get(), may include the flip bits.
/// @return Shape of the tile. An empty tile returns `tile_none`. A horizontally flipped tile turns
/// a left slope into a right slope and vice versa.
inline tile_shape tilemap_shape(const tilemap &map, i32 value) {
  if (value < 0)
    return tile_none;
  const i32 id = tile_id(value);
  tile_shape s = (usize)id < map.shapes.size() ? (tile_shape)map.shapes[(usize)id] : tile_solid;
  if ((value & tile_flip_x) != 0 && tile_is_slope(s)) {
    switch (s) {
    case tile_slope_r: s = tile_slope_l; break;
    case tile_slope_l: s = tile_slope_r; break;
    case tile_slope_r_low: s = tile_slope_l_low; break;
    case tile_slope_r_high: s = tile_slope_l_high; break;
    case tile_slope_l_low: s = tile_slope_r_low; break;
    case tile_slope_l_high: s = tile_slope_r_high; break;
    default: break;
    }
  }
  return s;
}

/// Make every tile with index `id` run an animation through the tiles `frames`, each frame lasting `seconds`
/// seconds. The engine draws the current frame without changing the tile in the grid.
/// @param map Tilemap.
/// @param id Base tile index, the tile placed in the grid.
/// @param frames Tile index of each frame.
/// @param seconds Duration of each frame, seconds.
inline void tilemap_animate(tilemap &map, i32 id, const std::vector<i32> &frames, f32 seconds) {
  if (id < 0 || frames.empty() || seconds <= 0.0f)
    return;
  tile_anim anim{frames, std::vector<f32>(frames.size(), seconds), seconds * (f32)frames.size()};
  map.anims[id] = std::move(anim);
}

/// Tile index currently shown by an animation at `time`.
/// @param anim Animation.
/// @param time Time, seconds.
/// @return The tile index, or -1 if the animation is empty.
inline i32 tile_anim_frame(const tile_anim &anim, f32 time) {
  if (anim.frames.empty() || anim.total <= 0.0f)
    return -1;
  f32 t = std::fmod(time, anim.total);
  if (t < 0.0f)
    t += anim.total;
  for (usize i = 0; i < anim.frames.size(); i++) {
    const f32 d = i < anim.durations.size() ? anim.durations[i] : 0.0f;
    if (t < d)
      return anim.frames[i];
    t -= d;
  }
  return anim.frames.back();
}

/// Result of tilemap_move().
struct move_result {
  vec2 pos{};         ///< New top-left position of the body.
  bool hit_x = false; ///< Blocked on the horizontal axis.
  bool hit_y = false; ///< Blocked on the vertical axis.
};

/// Round-down division, correct for negative numbers too: `floor_div(-1, 32) == -1`.
/// @param a Dividend.
/// @param b Divisor, positive.
/// @return Quotient rounded down.
constexpr i32 floor_div(i32 a, i32 b) {
  return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/// Key of the chunk at chunk coordinates `(cx, cy)`.
/// @param cx Chunk coordinate by column.
/// @param cy Chunk coordinate by row.
/// @return The key used in tilemap::chunks.
constexpr u64 tile_chunk_key(i32 cx, i32 cy) {
  return ((u64)(u32)cx << 32) | (u64)(u32)cy;
}

/// Chunk coordinates from a key.
/// @param key Key from tile_chunk_key().
/// @return Chunk coordinates.
constexpr cell tile_chunk_coord(u64 key) {
  return {(i32)(u32)(key >> 32), (i32)(u32)(key & 0xffffffffu)};
}

/// Tile at column `x`, row `y`.
/// @param map Tilemap.
/// @param x Column.
/// @param y Row.
/// @return Tile index in the tileset, or -1 if empty.
inline i32 tilemap_get(const tilemap &map, i32 x, i32 y) {
  const i32 cx = floor_div(x, tile_chunk_size);
  const i32 cy = floor_div(y, tile_chunk_size);
  const auto it = map.chunks.find(tile_chunk_key(cx, cy));
  if (it == map.chunks.end())
    return -1;
  const i32 lx = x - cx * tile_chunk_size;
  const i32 ly = y - cy * tile_chunk_size;
  return it->second.tiles[(usize)(ly * tile_chunk_size + lx)];
}

/// Set the tile at column `x`, row `y`. The chunk is created when needed and removed when it runs out of tiles.
/// @param map Tilemap.
/// @param x Column.
/// @param y Row.
/// @param id Tile index in the tileset, -1 is an empty tile.
inline void tilemap_set(tilemap &map, i32 x, i32 y, i32 id) {
  if (id < -1)
    id = -1;
  const i32 cx = floor_div(x, tile_chunk_size);
  const i32 cy = floor_div(y, tile_chunk_size);
  const u64 key = tile_chunk_key(cx, cy);
  auto it = map.chunks.find(key);
  if (it == map.chunks.end()) {
    if (id < 0)
      return; // clearing a tile that is already empty
    it = map.chunks.emplace(key, tile_chunk{}).first;
  }
  tile_chunk &chunk = it->second;
  i32 &slot = chunk.tiles[(usize)((y - cy * tile_chunk_size) * tile_chunk_size +
                                  (x - cx * tile_chunk_size))];
  if (slot == id)
    return;
  chunk.filled += (id >= 0 ? 1 : 0) - (slot >= 0 ? 1 : 0);
  slot = id;
  chunk.version = ++map.revision;
  if (chunk.filled == 0)
    map.chunks.erase(it);
}

/// Clear every tile.
/// @param map Tilemap.
inline void tilemap_clear(tilemap &map) {
  map.chunks.clear();
  ++map.revision;
}

/// One entry in the legend of a text map: which tile this character places. Used with
/// tilemap_from_text() and tilemap_from_rows().
///
/// Characters not in the legend: ` `, `.` and tab are empty tiles, every other character is
/// reported back to the game as a tile_marker (spawn point, enemy, coin).
struct tile_key {
  char symbol;         ///< Character in the text map.
  i32 tile;            ///< Tile index to place for this character. -1 clears the tile (empty tile).
  bool marker = false; ///< `true`: besides placing the tile, also report this character's position back to the game.
};

/// Position of a character that the text map uses only to **mark** something: player, enemy, coin.
/// Use njin::tilemap_cell_rect() to turn a tile into a position in the world.
struct tile_marker {
  char symbol; ///< The character that was found.
  cell at;     ///< Its tile, including the `origin` that was added.
};

/// @cond INTERNAL
inline void tilemap_apply_row(tilemap &map, std::string_view row, i32 y, std::initializer_list<tile_key> legend,
                              cell origin, std::vector<tile_marker> &markers) {
  for (usize i = 0; i < row.size(); i++) {
    const char c = row[i];
    const cell at{origin.x + (i32)i, origin.y + y};
    const tile_key *key = nullptr;
    for (const tile_key &k : legend)
      if (k.symbol == c) {
        key = &k;
        break;
      }
    if (key != nullptr) {
      tilemap_set(map, at.x, at.y, key->tile);
      if (key->marker)
        markers.push_back({c, at});
    } else if (c != ' ' && c != '.' && c != '\t') {
      markers.push_back({c, at});
    }
  }
}
/// @endcond

/// Build a map from a multi-line string, each character being a tile: the "traditional" way of writing maps, no need for
/// Tiled or LDtk. The string can be written directly in code or read from a file with njin::file_read().
/// @code
/// const auto markers = njin::tilemap_from_text(map, R"(
/// ####################
/// #..P......E........#
/// #.....####.........#
/// ####################
/// )", {{'#', 7}, {'P', 0, true}});
/// // '#' becomes tile 7. 'P' becomes tile 0 (the ground under its feet) and is reported back; 'E' is not in the legend so
/// // it is only reported back; '.' is an empty tile. Then create the player and the enemy at their tiles.
/// @endcode
/// Rules:
/// - the first line is row 0, the first character is column 0 (plus `origin`). Lines may differ in length;
/// - if the string starts with a newline, that newline is dropped, so you can write `R"(` and then start the first row on a new line.
///   A last line ending with a newline does not create an extra empty row; an empty line in the middle still counts as a row;
/// - Windows line endings (`\r\n`) are understood correctly;
/// - empty tiles (` `, `.`, tab) and marker-only characters **do not touch** an existing tile, so you can call it several
///   times to stack layers. To clear a tile, say so in the legend: `{'.', -1}`;
/// - characters in the legend call njin::tilemap_set(), so the tile's collision shape and animation (njin::tilemap_set_shape,
///   njin::tilemap_animate) apply as if it had been placed by hand.
/// @param map Tilemap to place tiles into. Needs `tileset` and `tile_size` like any other tilemap.
/// @param text Text map.
/// @param legend Legend. If empty, every character (except ` `, `.`, tab) is only reported back.
/// @param origin Tile of the first character. Defaults to `(0, 0)`.
/// @return The marker characters, in reading order: top to bottom, left to right.
inline std::vector<tile_marker> tilemap_from_text(tilemap &map, std::string_view text,
                                                  std::initializer_list<tile_key> legend = {}, cell origin = {}) {
  std::vector<tile_marker> markers;
  usize pos = 0;
  if (text.starts_with("\r\n"))
    pos = 2;
  else if (text.starts_with('\n'))
    pos = 1;
  i32 y = 0;
  while (pos < text.size()) {
    usize end = text.find('\n', pos);
    if (end == std::string_view::npos)
      end = text.size();
    std::string_view row = text.substr(pos, end - pos);
    if (!row.empty() && row.back() == '\r')
      row.remove_suffix(1);
    tilemap_apply_row(map, row, y, legend, origin, markers);
    pos = end + 1;
    y++;
  }
  return markers;
}

/// Like tilemap_from_text(), but each row is a separate string. More compact for small maps written directly in code,
/// and there is no need to worry about the newline at the start of the string.
/// @code
/// const auto markers = njin::tilemap_from_rows(map, {"#####", "#.P.#", "#####"}, {{'#', 7}});
/// @endcode
/// @param map Tilemap to place tiles into.
/// @param rows Rows, from top to bottom.
/// @param legend Legend. See njin::tile_key.
/// @param origin Tile of the first character. Defaults to `(0, 0)`.
/// @return The marker characters, in reading order.
inline std::vector<tile_marker> tilemap_from_rows(tilemap &map, std::initializer_list<std::string_view> rows,
                                                  std::initializer_list<tile_key> legend = {}, cell origin = {}) {
  std::vector<tile_marker> markers;
  i32 y = 0;
  for (const std::string_view row : rows)
    tilemap_apply_row(map, row, y++, legend, origin, markers);
  return markers;
}

/// Tile containing a point in the world.
/// @param map Tilemap.
/// @param origin Position of the top-left corner of tile (0, 0) in the world.
/// @param world_pos Point in the world.
/// @return The tile containing that point.
inline cell tilemap_cell_at(const tilemap &map, vec2 origin, vec2 world_pos) {
  const vec2 local = world_pos - origin;
  return {(i32)std::floor(local.x / map.tile_size.x),
          (i32)std::floor(local.y / map.tile_size.y)};
}

/// Rectangle of a tile in the world.
/// @param map Tilemap.
/// @param origin Position of the top-left corner of tile (0, 0) in the world.
/// @param x Column.
/// @param y Row.
/// @return The rectangle of the tile.
inline rect tilemap_cell_rect(const tilemap &map, vec2 origin, i32 x, i32 y) {
  return {{origin.x + (f32)x * map.tile_size.x,
           origin.y + (f32)y * map.tile_size.y},
          map.tile_size};
}

/// Whether a rectangle overlaps any non-empty tile. Only touching an edge does not
/// count.
/// @param map Tilemap.
/// @param origin Position of the top-left corner of tile (0, 0) in the world.
/// @param box Rectangle to check.
/// @return `true` if `box` overlaps at least one non-empty tile.
inline bool tilemap_overlaps(const tilemap &map, vec2 origin, rect box) {
  if (box.size.x <= 0.0f || box.size.y <= 0.0f)
    return false;
  const cell a = tilemap_cell_at(map, origin, box.pos);
  // Subtract a little so a right/bottom edge exactly on a tile edge is not counted in the next tile.
  const cell b =
      tilemap_cell_at(map, origin, box.pos + box.size - vec2{1e-4f, 1e-4f});
  for (i32 y = a.y; y <= b.y; y++) {
    for (i32 x = a.x; x <= b.x; x++) {
      if (tilemap_get(map, x, y) >= 0)
        return true;
    }
  }
  return false;
}

/// Move a rectangle through the tilemap, stopping when it hits a non-empty tile.
///
/// Moves along the horizontal axis first and then the vertical axis, so the body slides along walls
/// instead of sticking to them. This is the familiar approach for platformer games:
/// `hit_y && delta.y > 0` means standing on the ground.
///
/// Each axis should move no more than one tile per call, otherwise the body may pass
/// through thin walls.
///
/// Every non-empty tile blocks, tilemap::shapes is not considered. For one-way platforms
/// and slopes use a `collider_tiles` collider with collision_move().
/// @param map Tilemap, every non-empty tile is an obstacle.
/// @param origin Position of the top-left corner of tile (0, 0) in the world.
/// @param box Rectangle of the body to move.
/// @param delta Desired displacement this frame.
/// @return The new position and which axes were blocked.
inline move_result tilemap_move(const tilemap &map, vec2 origin, rect box,
                                vec2 delta) {
  move_result result{box.pos, false, false};
  // Push `r` out of every tile it overlaps, against the direction of `step` on one axis.
  const auto resolve = [&](rect &r, f32 step, bool horizontal) {
    const cell a = tilemap_cell_at(map, origin, r.pos);
    const cell b =
        tilemap_cell_at(map, origin, r.pos + r.size - vec2{1e-4f, 1e-4f});
    for (i32 y = a.y; y <= b.y; y++) {
      for (i32 x = a.x; x <= b.x; x++) {
        if (tilemap_get(map, x, y) < 0)
          continue;
        const rect c = tilemap_cell_rect(map, origin, x, y);
        if (horizontal) {
          if (step > 0.0f && r.pos.x + r.size.x > c.pos.x)
            r.pos.x = c.pos.x - r.size.x;
          else if (step < 0.0f && r.pos.x < c.pos.x + c.size.x)
            r.pos.x = c.pos.x + c.size.x;
        } else {
          if (step > 0.0f && r.pos.y + r.size.y > c.pos.y)
            r.pos.y = c.pos.y - r.size.y;
          else if (step < 0.0f && r.pos.y < c.pos.y + c.size.y)
            r.pos.y = c.pos.y + c.size.y;
        }
      }
    }
  };

  rect r{{box.pos.x + delta.x, box.pos.y}, box.size};
  if (delta.x != 0.0f && tilemap_overlaps(map, origin, r)) {
    resolve(r, delta.x, true);
    result.hit_x = true;
  }
  r.pos.y += delta.y;
  if (delta.y != 0.0f && tilemap_overlaps(map, origin, r)) {
    resolve(r, delta.y, false);
    result.hit_y = true;
  }
  result.pos = r.pos;
  return result;
}
/// @}
} // namespace njin
