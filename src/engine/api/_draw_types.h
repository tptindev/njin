#pragma once

// Kiểu nhỏ của phần vẽ mà header khác cần mà không cần cả njin_draw.h
// (njin_draw.h include header này).

namespace njin {
/// @addtogroup grp_texture
/// @{

/// Cách lấy mẫu khi texture bị co giãn.
enum texture_filter {
  filter_nearest, ///< Giữ nguyên từng pixel, sắc cạnh. Dùng cho pixel art.
  filter_linear,  ///< Làm mượt. Mặc định.
  /// Làm mượt ở mọi khoảng cách: tạo mipmap và lọc tam tuyến. Cho ảnh lặp lại
  /// trên mặt 3D lớn nhìn từ xa (tường, sàn), nơi `filter_linear` lấp lánh. Ảnh
  /// trong atlas và render texture lấy `filter_linear`.
  filter_mipmap,
};
/// @}

/// @addtogroup grp_draw
/// @{

/// Cách trộn màu của những gì vẽ sau blend_begin().
enum blend_mode {
  blend_alpha,    ///< Trộn theo độ trong suốt. Mặc định.
  blend_additive, ///< Cộng màu: sáng lên. Dùng cho lửa, ánh sáng, tia lửa.
  blend_multiply, ///< Nhân màu: tối đi. Dùng cho bóng đổ, lớp phủ màu.
};
/// @}
} // namespace njin
