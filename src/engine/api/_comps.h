#pragma once

#include "_types.h"
namespace njin {
/// @addtogroup grp_comps
/// @{

/// Vị trí, góc xoay và tỉ lệ của một entity trong thế giới.
///
/// Camera cũng đọc transform của chính entity mang nó (xem camera_2d).
struct transform {
  vec2 pos{};       ///< Vị trí trong thế giới.
  f32 rot = 0.0f;   ///< Góc xoay tính bằng độ, theo chiều kim đồng hồ.
  f32 scale = 1.0f; ///< Tỉ lệ. 1 là kích thước gốc.
};

/// Camera 2D. Cần một transform trên cùng entity: `transform.pos` là điểm
/// trong thế giới mà camera nhìn vào, `transform.rot` là góc xoay của nó.
/// `transform.scale` bị bỏ qua.
///
/// Camera chỉ có tác dụng khi entity đó cũng mang camera_on.
struct camera_2d {
  /// Vị trí trên màn hình (pixel) nơi `transform.pos` được vẽ. Dùng một nửa
  /// kích thước màn hình để camera luôn nằm giữa mục tiêu.
  vec2 offset{};
  /// 1 là không phóng, 2 là mọi thứ to gấp đôi. Giá trị <= 0 được coi là 1.
  f32 zoom = 1.0f;
};

/// Tag đánh dấu camera đang được dùng để vẽ và cho w2scr()/scr2w().
///
/// Nếu nhiều entity cùng có tag này thì cái tìm thấy đầu tiên được dùng. Nếu
/// không có entity nào, thế giới trùng với màn hình.
struct camera_on {};
/// @}
} // namespace njin
