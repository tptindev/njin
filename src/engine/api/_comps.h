#pragma once

#include "_math.h"
#include "_types.h"
namespace njin {
/// @addtogroup grp_comps
/// @{

/// Vị trí, góc xoay và tỉ lệ của một entity trong thế giới.
///
/// Camera cũng đọc transform của chính entity mang nó (xem camera_2d).
/// Sprite dùng cả ba trường khi vẽ.
struct transform {
  vec2 pos{};       ///< Vị trí trong thế giới.
  f32 rot = 0.0f;   ///< Góc xoay tính bằng độ, theo chiều kim đồng hồ.
  f32 scale = 1.0f; ///< Tỉ lệ. 1 là kích thước gốc.
};

/// 2D camera. Cần một transform trên cùng entity: `transform.pos` là điểm
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

/// Ảnh vẽ tại vị trí của entity. Cần một transform trên cùng entity.
///
/// Module sprite của engine vẽ mọi entity có transform và sprite trong
/// `phase_render`, theo thứ tự `layer` tăng dần. System vẽ của game trong
/// `phase_render` chạy sau, nên vẽ đè lên sprite.
struct sprite {
  texture_handle texture{}; ///< Ảnh cần vẽ.
  /// Vùng trong ảnh, tính bằng pixel. Kích thước 0 là cả ảnh. sprite_anim ghi
  /// đè trường này mỗi frame.
  rect source{};
  /// Điểm neo, tính theo tỉ lệ kích thước: `{0, 0}` là góc trên trái, `{0.5,
  /// 0.5}` là tâm. `transform.pos` rơi đúng vào điểm này, và ảnh xoay quanh nó.
  vec2 origin{0.5f, 0.5f};
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nhân vào ảnh.
  i32 layer = 0;       ///< Lớp vẽ: lớp nhỏ vẽ trước, lớp lớn đè lên.
  bool flip_x = false; ///< Lật ngang.
  bool flip_y = false; ///< Lật dọc.
  bool visible = true; ///< Ẩn mà không cần gỡ component.
};

/// Animation theo frame từ một sprite sheet. Cần sprite trên cùng entity.
///
/// Các frame có cùng kích thước `frame_size`, xếp theo hàng từ trái sang phải
/// rồi từ trên xuống, đánh số từ 0. Module sprite của engine chuyển frame mỗi
/// frame và ghi `sprite.source`. Dùng anim_play() để đổi animation.
struct sprite_anim {
  vec2 frame_size{};    ///< Kích thước một frame, tính bằng pixel.
  i32 first = 0;        ///< Số thứ tự frame đầu tiên của animation.
  i32 count = 1;        ///< Số frame của animation.
  f32 fps = 10.0f;      ///< Số frame mỗi giây.
  bool loop = true;     ///< Lặp lại khi hết.
  bool playing = true;  ///< Đang chạy. Đặt `false` để dừng ở frame hiện tại.
  f32 time = 0.0f;      ///< Thời gian đã chạy, do engine cập nhật.
  i32 frame = 0;        ///< Frame hiện tại, từ 0 đến `count - 1`.
  bool finished = false; ///< Đã chạy hết (chỉ khi không lặp).
};

/// Đổi sang một animation khác trong cùng sprite sheet.
///
/// Nếu đang chạy đúng animation này thì không làm gì, nên gọi mỗi frame được:
/// animation không bị bắt đầu lại liên tục.
/// @param anim Animation cần đổi.
/// @param first Frame đầu tiên.
/// @param count Số frame.
/// @param fps Số frame mỗi giây.
/// @param loop Có lặp lại không.
inline void anim_play(sprite_anim &anim, i32 first, i32 count, f32 fps,
                      bool loop = true) {
  if (anim.first == first && anim.count == count && anim.playing &&
      !anim.finished)
    return;
  anim.first = first;
  anim.count = count;
  anim.fps = fps;
  anim.loop = loop;
  anim.playing = true;
  anim.time = 0.0f;
  anim.frame = 0;
  anim.finished = false;
}

/// Gắn entity vào một scene. Khi rời scene đó, engine tự hủy entity.
///
/// Xem scene_set().
struct scene_owned {
  scene_handle scene{}; ///< Scene sở hữu entity.
};
/// @}
} // namespace njin
