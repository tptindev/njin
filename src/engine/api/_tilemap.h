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

/// Số ô mỗi cạnh của một chunk. Một chunk có 32 x 32 ô.
inline constexpr i32 tile_chunk_size = 32;

/// Bit đánh dấu ô bị lật ngang, cộng vào số thứ tự ô: `id | tile_flip_x`.
inline constexpr i32 tile_flip_x = 1 << 29;
/// Bit đánh dấu ô bị lật dọc, cộng vào số thứ tự ô: `id | tile_flip_y`.
inline constexpr i32 tile_flip_y = 1 << 30;

/// Số thứ tự ô trong tileset, bỏ các bit lật.
/// @param value Giá trị của ô, từ tilemap_get().
/// @return Số thứ tự ô, hoặc -1 nếu ô trống.
constexpr i32 tile_id(i32 value) {
  return value < 0 ? -1 : value & ~(tile_flip_x | tile_flip_y);
}

/// Một khối 32 x 32 ô của tilemap.
///
/// Tilemap chỉ giữ những chunk có ít nhất một ô không trống, nên bản đồ rộng
/// mà thưa vẫn nhẹ, và tọa độ ô có thể âm.
struct tile_chunk {
  /// Các ô theo hàng, -1 là ô trống.
  std::array<i32, (usize)tile_chunk_size * tile_chunk_size> tiles;
  i32 filled = 0;  ///< Số ô không trống. Chunk về 0 thì bị xóa.
  u32 version = 0; ///< Tăng mỗi lần chunk đổi, để engine biết cần vẽ lại.

  tile_chunk() { tiles.fill(-1); }
};

/// Vị trí một ô trong lưới. Có thể âm.
struct cell {
  i32 x = 0; ///< Cột.
  i32 y = 0; ///< Hàng.
};

/// Hình va chạm của một loại ô, dùng bởi collision_move() và collision_raycast()
/// với `collider_tiles`. Đặt bằng tilemap_set_shape(); ô chưa đặt là `tile_solid`.
///
/// Dốc là của game platformer: mặt dốc chỉ đỡ từ phía trên, và phía đáy của ô
/// vẫn là vật cản. Tên `_r` là dốc **cao bên phải** (đi sang phải là lên dốc),
/// `_l` là cao bên trái. Dốc 22.5 độ chiếm hai ô liền nhau: `_low` (nửa dưới)
/// rồi `_high` (nửa trên).
enum tile_shape : u8 {
  tile_solid = 0,    ///< Chặn mọi phía. Mặc định.
  tile_none,         ///< Không va chạm: chỉ để nhìn, dù nằm trong layer vật cản.
  tile_one_way,      ///< Bục một chiều: chỉ đỡ từ trên xuống, nhảy xuyên từ dưới lên được.
  tile_slope_r,      ///< Dốc 45 độ, thấp bên trái, cao bên phải.
  tile_slope_l,      ///< Dốc 45 độ, cao bên trái, thấp bên phải.
  tile_slope_r_low,  ///< Dốc 22.5 độ cao bên phải, nửa dưới (0 đến nửa ô).
  tile_slope_r_high, ///< Dốc 22.5 độ cao bên phải, nửa trên (nửa ô đến đầy ô).
  tile_slope_l_low,  ///< Dốc 22.5 độ cao bên trái, nửa dưới.
  tile_slope_l_high, ///< Dốc 22.5 độ cao bên trái, nửa trên.
  tile_shape_count   ///< Số loại hình. Không phải một hình thật.
};

/// Ô có phải dốc không.
/// @param shape Hình của ô.
/// @return `true` với các `tile_slope_*`.
constexpr bool tile_is_slope(tile_shape shape) {
  return shape >= tile_slope_r && shape <= tile_slope_l_high;
}

/// Chiều cao mặt dốc tại một điểm trong ô, tính từ đáy ô theo tỉ lệ chiều cao ô.
/// @param shape Hình của ô.
/// @param u Vị trí ngang trong ô, 0 là mép trái, 1 là mép phải.
/// @return 0 (mặt ở đáy ô) đến 1 (mặt ở đỉnh ô). Ô không phải dốc trả về 1.
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

/// Đọc tên một hình ô, như trong thuộc tính của Tiled hay tên giá trị IntGrid
/// của LDtk: `solid`, `none` (hoặc `empty`), `one_way` (hoặc `oneway`,
/// `platform`), `slope_r`, `slope_l`, `slope_r_low`, `slope_r_high`,
/// `slope_l_low`, `slope_l_high`. Không phân biệt hoa thường; `-` và dấu cách
/// được coi như `_`.
/// @param name Tên.
/// @param out Nhận hình nếu tên hợp lệ.
/// @return `true` nếu nhận ra tên.
bool tile_shape_from_name(const char *name, tile_shape &out);

/// Animation của một loại ô: nước, đuốc, cỏ lay. Mọi ô cùng số thứ tự trong
/// tilemap chạy cùng nhịp. Tạo bằng tilemap_animate(); nạp tự động từ Tiled.
struct tile_anim {
  std::vector<i32> frames;    ///< Số thứ tự ô của từng frame trong tileset.
  std::vector<f32> durations; ///< Thời lượng từng frame, giây.
  f32 total = 0.0f;           ///< Tổng thời lượng, tính sẵn.
};

/// Lưới ô vuông không giới hạn kích thước, vẽ từ một tileset, chia thành chunk.
///
/// Cần một transform trên cùng entity: `transform.pos` là góc trên trái của ô
/// (0, 0) trong thế giới. Góc xoay và tỉ lệ bị bỏ qua.
///
/// Tileset là một ảnh gồm các ô cùng kích thước `tile_size`, đánh số từ 0 theo
/// hàng từ trái sang phải rồi từ trên xuống, có thể cách mép ảnh `margin` và
/// cách nhau `spacing` pixel (như tileset của Tiled và LDtk). Mỗi ô của lưới
/// giữ số thứ tự ô trong tileset, hoặc -1 là ô trống. Cộng thêm njin::tile_flip_x
/// hoặc njin::tile_flip_y để lật ô khi vẽ; đọc số thứ tự bằng tile_id().
///
/// **Chunking.** Ô được lưu theo khối 32 x 32 (njin::tile_chunk). Module sprite
/// của engine vẽ sẵn mỗi chunk vào một ảnh riêng và chỉ vẽ lại chunk có ô vừa
/// đổi, nên một bản đồ lớn chỉ tốn vài lệnh vẽ mỗi frame. Chỉ chunk nằm trong
/// khung nhìn của camera mới được vẽ, và ảnh của chunk khuất lâu sẽ được giải
/// phóng.
///
/// Luôn đổi ô bằng tilemap_set() để engine biết chunk nào cần vẽ lại.
struct tilemap {
  texture_handle tileset{};     ///< Ảnh tileset.
  vec2 tile_size{16.0f, 16.0f}; ///< Kích thước một ô, tính bằng pixel.
  f32 margin = 0.0f;  ///< Khoảng từ mép ảnh tileset đến ô đầu tiên, pixel.
  f32 spacing = 0.0f; ///< Khoảng giữa hai ô liền nhau trong ảnh tileset, pixel.
  i32 layer = 0;                ///< Lớp vẽ, cùng thang với sprite::layer.
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nhân vào mọi ô.
  bool visible = true;          ///< Ẩn khi vẽ. Vẫn dùng được cho va chạm.
  /// Các chunk có ô, theo khóa từ tile_chunk_key().
  std::unordered_map<u64, tile_chunk> chunks;
  u32 revision = 0; ///< Tăng mỗi lần có ô đổi.
  /// Hình va chạm theo số thứ tự ô: `shapes[id]`. Ô ngoài danh sách là
  /// `tile_solid`. Đặt bằng tilemap_set_shape().
  std::vector<u8> shapes;
  /// Animation theo số thứ tự ô gốc. Đặt bằng tilemap_animate().
  std::unordered_map<i32, tile_anim> anims;
};

/// Đặt hình va chạm cho mọi ô có số thứ tự `id` trong tilemap.
/// @param map Tilemap.
/// @param id Số thứ tự ô trong tileset (không kèm bit lật).
/// @param shape Hình.
inline void tilemap_set_shape(tilemap &map, i32 id, tile_shape shape) {
  if (id < 0)
    return;
  if ((usize)id >= map.shapes.size())
    map.shapes.resize((usize)id + 1, tile_solid);
  map.shapes[(usize)id] = shape;
}

/// Hình va chạm của một ô.
/// @param map Tilemap.
/// @param value Giá trị ô từ tilemap_get(), có thể kèm bit lật.
/// @return Hình của ô. Ô trống trả về `tile_none`. Ô lật ngang đổi dốc trái
/// thành dốc phải và ngược lại.
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

/// Cho mọi ô số `id` chạy animation qua các ô `frames`, mỗi frame `seconds`
/// giây. Engine vẽ frame hiện tại mà không cần đổi ô trong lưới.
/// @param map Tilemap.
/// @param id Số thứ tự ô gốc, là ô đặt trong lưới.
/// @param frames Số thứ tự ô của từng frame.
/// @param seconds Thời lượng mỗi frame, giây.
inline void tilemap_animate(tilemap &map, i32 id, const std::vector<i32> &frames, f32 seconds) {
  if (id < 0 || frames.empty() || seconds <= 0.0f)
    return;
  tile_anim anim{frames, std::vector<f32>(frames.size(), seconds), seconds * (f32)frames.size()};
  map.anims[id] = std::move(anim);
}

/// Số thứ tự ô đang hiện của một animation tại thời điểm `time`.
/// @param anim Animation.
/// @param time Thời gian, giây.
/// @return Số thứ tự ô, hoặc -1 nếu animation rỗng.
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

/// Kết quả của tilemap_move().
struct move_result {
  vec2 pos{};         ///< Vị trí góc trên trái mới của vật.
  bool hit_x = false; ///< Bị chặn theo trục ngang.
  bool hit_y = false; ///< Bị chặn theo trục dọc.
};

/// Chia làm tròn xuống, đúng cả với số âm: `floor_div(-1, 32) == -1`.
/// @param a Số bị chia.
/// @param b Số chia, dương.
/// @return Thương làm tròn xuống.
constexpr i32 floor_div(i32 a, i32 b) {
  return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/// Khóa của chunk tại tọa độ chunk `(cx, cy)`.
/// @param cx Tọa độ chunk theo cột.
/// @param cy Tọa độ chunk theo hàng.
/// @return Khóa dùng trong tilemap::chunks.
constexpr u64 tile_chunk_key(i32 cx, i32 cy) {
  return ((u64)(u32)cx << 32) | (u64)(u32)cy;
}

/// Tọa độ chunk từ khóa.
/// @param key Khóa từ tile_chunk_key().
/// @return Tọa độ chunk.
constexpr cell tile_chunk_coord(u64 key) {
  return {(i32)(u32)(key >> 32), (i32)(u32)(key & 0xffffffffu)};
}

/// Ô tại cột `x`, hàng `y`.
/// @param map Tilemap.
/// @param x Cột.
/// @param y Hàng.
/// @return Số thứ tự ô trong tileset, hoặc -1 nếu trống.
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

/// Đặt ô tại cột `x`, hàng `y`. Chunk được tạo khi cần và bị xóa khi hết ô.
/// @param map Tilemap.
/// @param x Cột.
/// @param y Hàng.
/// @param id Chỉ số ô trong tileset, -1 là ô trống.
inline void tilemap_set(tilemap &map, i32 x, i32 y, i32 id) {
  if (id < -1)
    id = -1;
  const i32 cx = floor_div(x, tile_chunk_size);
  const i32 cy = floor_div(y, tile_chunk_size);
  const u64 key = tile_chunk_key(cx, cy);
  auto it = map.chunks.find(key);
  if (it == map.chunks.end()) {
    if (id < 0)
      return; // xóa một ô vốn đã trống
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

/// Xóa mọi ô.
/// @param map Tilemap.
inline void tilemap_clear(tilemap &map) {
  map.chunks.clear();
  ++map.revision;
}

/// Một dòng trong bảng ký tự của bản đồ chữ: ký tự này đặt ô nào. Dùng với
/// tilemap_from_text() và tilemap_from_rows().
///
/// Ký tự không có trong bảng: ` `, `.` và tab là ô trống, mọi ký tự khác được
/// báo lại cho game như một tile_marker (điểm xuất hiện, kẻ địch, đồng xu).
struct tile_key {
  char symbol;         ///< Ký tự trong bản đồ chữ.
  i32 tile;            ///< Số thứ tự ô đặt cho ký tự này. -1 là xóa ô (ô trống).
  bool marker = false; ///< `true`: ngoài việc đặt ô, còn báo vị trí ký tự này về cho game.
};

/// Vị trí của một ký tự mà bản đồ chữ chỉ dùng để **đánh dấu**: người chơi, kẻ địch, đồng xu.
/// Dùng njin::tilemap_cell_rect() để đổi ô thành vị trí trong thế giới.
struct tile_marker {
  char symbol; ///< Ký tự đã gặp.
  cell at;     ///< Ô của nó, kể cả `origin` đã cộng vào.
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

/// Dựng bản đồ từ một chuỗi nhiều dòng, mỗi ký tự là một ô: cách viết bản đồ "kiểu truyền thống", không cần
/// Tiled hay LDtk. Chuỗi có thể viết thẳng trong code hoặc đọc từ file bằng njin::file_read().
/// @code
/// const auto markers = njin::tilemap_from_text(map, R"(
/// ####################
/// #..P......E........#
/// #.....####.........#
/// ####################
/// )", {{'#', 7}, {'P', 0, true}});
/// // '#' thành ô số 7. 'P' thành ô số 0 (nền dưới chân) và được báo lại; 'E' không có trong bảng nên
/// // chỉ được báo lại; '.' là ô trống. Rồi tạo người chơi và kẻ địch ở đúng ô của chúng.
/// @endcode
/// Quy tắc:
/// - dòng đầu là hàng 0, ký tự đầu là cột 0 (cộng thêm `origin`). Các dòng có thể dài ngắn khác nhau;
/// - nếu chuỗi bắt đầu bằng xuống dòng thì xuống dòng đó bị bỏ, để viết `R"(` rồi xuống dòng mới đến hàng đầu.
///   Dòng cuối kết thúc bằng xuống dòng không tạo thêm một hàng rỗng; dòng rỗng ở giữa vẫn tính là một hàng;
/// - kết thúc dòng kiểu Windows (`\r\n`) được hiểu đúng;
/// - ô trống (` `, `.`, tab) và ký tự chỉ đánh dấu **không đụng** tới ô đang có, nên gọi nhiều lần để chồng
///   các lớp được. Muốn xóa ô thì ghi rõ trong bảng: `{'.', -1}`;
/// - ký tự trong bảng gọi njin::tilemap_set() nên hình va chạm và animation của ô (njin::tilemap_set_shape,
///   njin::tilemap_animate) áp dụng như khi đặt bằng tay.
/// @param map Tilemap cần đặt ô. Cần `tileset` và `tile_size` như mọi tilemap khác.
/// @param text Bản đồ chữ.
/// @param legend Bảng ký tự. Để trống thì mọi ký tự (trừ ` `, `.`, tab) chỉ được báo lại.
/// @param origin Ô của ký tự đầu tiên. Mặc định `(0, 0)`.
/// @return Các ký tự đánh dấu, theo thứ tự đọc: từ trên xuống, trái sang phải.
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

/// Như tilemap_from_text(), nhưng mỗi hàng là một chuỗi riêng. Gọn hơn cho bản đồ nhỏ viết ngay trong code,
/// và không phải lo chuyện xuống dòng đầu chuỗi.
/// @code
/// const auto markers = njin::tilemap_from_rows(map, {"#####", "#.P.#", "#####"}, {{'#', 7}});
/// @endcode
/// @param map Tilemap cần đặt ô.
/// @param rows Các hàng, từ trên xuống.
/// @param legend Bảng ký tự. Xem njin::tile_key.
/// @param origin Ô của ký tự đầu tiên. Mặc định `(0, 0)`.
/// @return Các ký tự đánh dấu, theo thứ tự đọc.
inline std::vector<tile_marker> tilemap_from_rows(tilemap &map, std::initializer_list<std::string_view> rows,
                                                  std::initializer_list<tile_key> legend = {}, cell origin = {}) {
  std::vector<tile_marker> markers;
  i32 y = 0;
  for (const std::string_view row : rows)
    tilemap_apply_row(map, row, y++, legend, origin, markers);
  return markers;
}

/// Ô chứa một điểm trong thế giới.
/// @param map Tilemap.
/// @param origin Vị trí góc trên trái của ô (0, 0) trong thế giới.
/// @param world_pos Điểm trong thế giới.
/// @return Ô chứa điểm đó.
inline cell tilemap_cell_at(const tilemap &map, vec2 origin, vec2 world_pos) {
  const vec2 local = world_pos - origin;
  return {(i32)std::floor(local.x / map.tile_size.x),
          (i32)std::floor(local.y / map.tile_size.y)};
}

/// Hình chữ nhật của một ô trong thế giới.
/// @param map Tilemap.
/// @param origin Vị trí góc trên trái của ô (0, 0) trong thế giới.
/// @param x Cột.
/// @param y Hàng.
/// @return Hình chữ nhật của ô.
inline rect tilemap_cell_rect(const tilemap &map, vec2 origin, i32 x, i32 y) {
  return {{origin.x + (f32)x * map.tile_size.x,
           origin.y + (f32)y * map.tile_size.y},
          map.tile_size};
}

/// Hình chữ nhật có chồng lên ô không trống nào không. Chỉ chạm mép thì không
/// tính.
/// @param map Tilemap.
/// @param origin Vị trí góc trên trái của ô (0, 0) trong thế giới.
/// @param box Hình chữ nhật cần kiểm tra.
/// @return `true` nếu `box` chồng lên ít nhất một ô không trống.
inline bool tilemap_overlaps(const tilemap &map, vec2 origin, rect box) {
  if (box.size.x <= 0.0f || box.size.y <= 0.0f)
    return false;
  const cell a = tilemap_cell_at(map, origin, box.pos);
  // Trừ một chút để cạnh phải/dưới chạm đúng mép ô không bị tính sang ô kế.
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

/// Di chuyển một hình chữ nhật trong tilemap, dừng lại khi đụng ô không trống.
///
/// Đi theo trục ngang trước rồi mới đến trục dọc, nên vật trượt dọc theo tường
/// thay vì dính vào. Đây là cách làm quen thuộc cho game platformer:
/// `hit_y && delta.y > 0` nghĩa là đang đứng trên mặt đất.
///
/// Mỗi trục nên dời không quá một ô mỗi lần gọi, nếu không vật có thể xuyên
/// qua tường mỏng.
///
/// Mọi ô không trống đều chặn, không xét tilemap::shapes. Muốn bục một chiều
/// và dốc thì dùng collider `collider_tiles` với collision_move().
/// @param map Tilemap, mọi ô không trống đều là vật cản.
/// @param origin Vị trí góc trên trái của ô (0, 0) trong thế giới.
/// @param box Hình chữ nhật của vật cần di chuyển.
/// @param delta Độ dời mong muốn trong frame này.
/// @return Vị trí mới và trục nào bị chặn.
inline move_result tilemap_move(const tilemap &map, vec2 origin, rect box,
                                vec2 delta) {
  move_result result{box.pos, false, false};
  // Đẩy `r` ra khỏi mọi ô nó chồng lên, ngược hướng của `step` trên một trục.
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
