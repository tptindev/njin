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
struct njin_ctx;

/// @addtogroup grp_level
/// @{

/// Định danh của một level đã nạp, trả về từ level_load().
///
/// `id == 0` là handle không hợp lệ (nạp thất bại). Level đã unload cũng bị bỏ qua.
struct level_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Cách nạp một level, dùng với level_load() và level_load_ldtk().
struct level_desc {
  /// Vị trí trong thế giới của góc trên trái level.
  vec2 origin{};
  /// Với LDtk: cộng thêm vị trí của level trong world (`worldX`, `worldY`), để
  /// nạp nhiều level liền nhau như trong editor.
  bool use_world_position = false;
  /// Lớp vẽ (njin::sprite::layer) của layer dưới cùng. Mỗi layer phía trên
  /// cao hơn một: layer thứ `i` từ dưới lên vẽ ở `layer_base + i`.
  i32 layer_base = 0;
  /// Tên các layer làm vật cản. Ô của chúng thành `collider_tiles`, object
  /// không có prefab thành collider hộp hoặc tròn. Để trống thì dùng quy tắc
  /// mặc định: layer có thuộc tính `solid = true` (Tiled), hoặc có tên chứa
  /// "collision", "collide", "solid" hoặc "wall" (không phân biệt hoa thường).
  std::vector<std::string> solid_layers{};
  /// Mẫu collider cho vật cản: `layer`, `mask` được giữ, hình được đặt lại.
  collider solid{};
  /// Cách lấy mẫu ảnh tileset. Mặc định `filter_nearest`: pixel art sắc nét,
  /// và không bị viền mờ giữa các ô.
  texture_filter filter = filter_nearest;
  /// Gắn mọi entity của level vào scene đang chạy (njin::scene_owned). Khi rời
  /// scene, entity bị hủy và level tự được unload.
  bool scene_owned = true;
};

/// Một object đặt trong editor: điểm xuất hiện, cửa, vùng, quái...
///
/// Mỗi object thành một entity có transform (tâm của object) và component
/// này. Nếu có prefab trùng tên với `type` (lớp của object trong Tiled, tên
/// entity trong LDtk), entity được dựng bằng prefab đó; component này được
/// gắn **trước** khi hàm dựng chạy, nên hàm dựng đọc được thuộc tính:
/// @code
/// void build_door(njin::njin_ctx &ctx, entt::entity e) {
///   const auto &obj = njin::world(ctx).get<njin::level_object>(e);
///   const char *target = obj.props["target"].string_or("start");
///   ...
/// }
/// @endcode
struct level_object {
  std::string name; ///< Tên (Tiled: name; LDtk: iid).
  std::string type; ///< Lớp (Tiled: class hoặc type; LDtk: tên entity).
  vec2 size{};      ///< Kích thước, pixel. 0 với điểm.
  /// Đỉnh của đa giác hoặc đường gấp khúc, so với `transform.pos`. Rỗng với
  /// hình chữ nhật, hình elip và điểm.
  std::vector<vec2> points{};
  bool ellipse = false; ///< Object là hình elip (Tiled).
  bool closed = true;   ///< Với `points`: đa giác (đóng) hay đường gấp khúc (mở).
  json_value props{};   ///< Thuộc tính tùy chỉnh (Tiled: properties; LDtk: field).
  level_handle level{}; ///< Level chứa object.
};

/// Nạp một bản đồ và tạo entity cho nó.
///
/// Nhận file từ **Tiled** (`.tmx`, `.tmj`, hoặc `.json` xuất từ Tiled) và
/// **LDtk** (`.ldtk`, nạp level đầu tiên; dùng level_load_ldtk() để chọn
/// level). Tileset, ảnh và level tách file được tìm cạnh file bản đồ.
///
/// Tạo ra:
/// - mỗi layer ô: một entity có transform và njin::tilemap (một layer dùng
///   nhiều tileset thì một entity cho mỗi tileset); layer vật cản có thêm
///   njin::collider `collider_tiles`;
/// - mỗi layer IntGrid của LDtk: một tilemap ẩn, giá trị ô là giá trị IntGrid
///   (1, 2, ...), đọc bằng tilemap_get(); layer vật cản có thêm collider;
/// - mỗi object: một entity có transform và njin::level_object, dựng bằng
///   prefab trùng tên với lớp của nó nếu có. Object hình ô (tile object) có
///   thêm njin::sprite.
///
/// Chỉ nhận bản đồ lưới vuông (orthogonal), loại mà game top-down và
/// platformer dùng; bản đồ isometric, lục giác bị từ chối (trả về handle id 0,
/// có ghi log). Hỗ trợ bản đồ vô hạn, layer lồng nhau, dữ liệu CSV, base64,
/// zlib, gzip. Chưa hỗ trợ (có cảnh báo): nén zstd, ô xoay chéo, tileset nhiều
/// ảnh, ô animation.
/// @param ctx Context của engine.
/// @param path Đường dẫn file bản đồ.
/// @param desc Cách nạp.
/// @return Handle của level, hoặc handle id 0 nếu lỗi (có ghi log).
level_handle level_load(njin_ctx &ctx, const char *path, const level_desc &desc = {});

/// Nạp một level cụ thể từ project LDtk.
/// @param ctx Context của engine.
/// @param path Đường dẫn file `.ldtk`.
/// @param level Tên level (identifier), hoặc null cho level đầu tiên.
/// @param desc Cách nạp.
/// @return Handle của level, hoặc handle id 0 nếu lỗi.
level_handle level_load_ldtk(njin_ctx &ctx, const char *path, const char *level,
                             const level_desc &desc = {});

/// Tên mọi level trong một project LDtk, theo thứ tự trong editor.
/// @param path Đường dẫn file `.ldtk`.
/// @param out Nhận các tên (được thêm vào cuối).
/// @return `false` nếu không đọc được file.
bool level_list_ldtk(const char *path, std::vector<std::string> &out);

/// Hủy mọi entity mà level đã tạo và giải phóng ảnh của nó. Level gắn với
/// scene (mặc định) tự được unload khi rời scene.
/// @param ctx Context của engine.
/// @param level Level. Handle không hợp lệ bị bỏ qua.
void level_unload(njin_ctx &ctx, level_handle level);

/// Kích thước level, pixel. @param ctx Context của engine. @param level Level.
/// @return Kích thước, hoặc `{0, 0}` nếu handle không hợp lệ.
vec2 level_size(const njin_ctx &ctx, level_handle level);

/// Góc trên trái của level trong thế giới. @param ctx Context của engine.
/// @param level Level. @return Vị trí.
vec2 level_origin(const njin_ctx &ctx, level_handle level);

/// Thuộc tính tùy chỉnh của cả bản đồ (Tiled) hoặc của level (LDtk).
/// @param ctx Context của engine.
/// @param level Level.
/// @return Object JSON, hoặc giá trị null nếu handle không hợp lệ.
const json_value &level_properties(const njin_ctx &ctx, level_handle level);

/// Tìm object đầu tiên của level có tên (hoặc, nếu không có tên nào khớp,
/// lớp) là `name`. Tiện cho điểm xuất hiện: `level_find(ctx, lv, "spawn")`.
/// @param ctx Context của engine.
/// @param level Level.
/// @param name Tên hoặc lớp.
/// @return Entity, hoặc `entt::null`.
entt::entity level_find(njin_ctx &ctx, level_handle level, const char *name);
/// @}
} // namespace njin
