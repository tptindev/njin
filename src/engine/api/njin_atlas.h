#pragma once
#include "_types.h"
#include "njin_draw.h"

namespace njin {
struct context;

/// @addtogroup grp_texture
/// @{

/// Cách tạo một atlas, xem atlas_create().
struct atlas_desc {
  /// Cạnh của mỗi trang atlas, pixel. Ảnh lớn hơn cạnh này (kể cả viền) không
  /// vào được atlas và được nạp như texture riêng.
  i32 size = 2048;
  /// Viền trống quanh mỗi ảnh, pixel: chép lại điểm ảnh ngoài cùng ra viền để
  /// ảnh không lem màu ảnh bên cạnh khi bị phóng, thu nhỏ hoặc xoay. 0 là không viền.
  i32 padding = 1;
  /// Cách lấy mẫu của cả atlas. Mọi ảnh trong atlas dùng chung một bộ lọc.
  texture_filter filter = filter_linear;
};

/// Tạo một atlas: nơi ghép nhiều ảnh nhỏ vào một texture lớn.
///
/// Raylib gom các lệnh vẽ liền nhau dùng chung texture thành một. Sprite xếp
/// theo layer hoặc theo y mà dùng nhiều texture khác nhau thì lệnh vẽ bị ngắt
/// mỗi lần đổi texture; ảnh cùng atlas là cùng texture, nên các lệnh đó nhập lại
/// thành một. Xem số lệnh vẽ ước tính trong njin_inspector.
///
/// Ảnh nạp bằng atlas_load() cho ra texture_handle thường, dùng được ở mọi nơi
/// nhận texture_handle: sprite, tilemap, hạt, UI, texture_draw().
/// @param ctx Context của engine.
/// @param desc Kích thước trang, viền và bộ lọc.
/// @return Handle của atlas.
atlas_handle atlas_create(context &ctx, const atlas_desc &desc = {});

/// Nạp một file ảnh và xếp vào atlas.
///
/// Atlas thêm trang mới khi trang cũ hết chỗ. Ảnh chỉ xếp vào atlas được nếu
/// vừa một trang; ảnh lớn hơn được nạp như texture_load() và có cảnh báo trong
/// log.
///
/// Khác với texture_load():
/// - texture_size() trả về kích thước của ảnh, không phải của cả trang.
/// - Shader riêng của game (shader_begin()) lấy mẫu `texture0` với toạ độ trên
///   cả trang atlas, không phải trên ảnh, nên shader đọc toạ độ 0..1 của ảnh sẽ
///   sai. Với shader như vậy, hãy nạp ảnh bằng texture_load().
/// - texture_set_filter() đổi bộ lọc của cả trang.
/// - Hot reload không áp dụng.
/// - texture_unload() bỏ handle nhưng không thu hồi chỗ trong atlas.
/// @param ctx Context của engine.
/// @param atlas Atlas cần xếp vào.
/// @param path Đường dẫn file ảnh.
/// @return Handle của texture, hoặc handle có id 0 nếu file thiếu hoặc không giải mã được.
texture_handle atlas_load(context &ctx, atlas_handle atlas, const char *path);

/// Hủy atlas: giải phóng các trang. Mọi texture_handle lấy từ atlas này thành
/// không hợp lệ. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param atlas Atlas cần hủy.
void atlas_destroy(context &ctx, atlas_handle atlas);
/// @}
} // namespace njin
