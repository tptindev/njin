#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_draw.h"
#include <entt/entity/fwd.hpp>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_particles
/// @{

/// Khoảng giá trị. Mỗi hạt lấy một giá trị ngẫu nhiên trong `[min, max]`.
/// `min == max` là giá trị cố định.
struct f32_range {
  f32 min = 0.0f; ///< Cận dưới.
  f32 max = 0.0f; ///< Cận trên.
};

/// Hình của hạt khi không có texture.
enum particle_shape {
  particle_circle, ///< Hình tròn, `size` là đường kính.
  particle_square, ///< Hình vuông, xoay theo `spin`.
};

/// Một hạt đang sống. Do engine tạo và cập nhật; game chỉ cần đọc nếu muốn.
struct particle {
  vec2 pos{};        ///< Vị trí (trong thế giới, hoặc so với emitter nếu `local_space`).
  vec2 velocity{};   ///< Vận tốc, đơn vị mỗi giây.
  f32 age = 0.0f;    ///< Đã sống bao lâu, giây.
  f32 life = 1.0f;   ///< Sống tổng cộng bao lâu, giây.
  f32 rot = 0.0f;    ///< Góc xoay, độ.
  f32 spin = 0.0f;   ///< Tốc độ xoay, độ mỗi giây.
  f32 size = 1.0f;   ///< Hệ số kích thước riêng của hạt, nhân vào size_start/size_end.
};

/// Nguồn phát hạt: nổ, bụi, tia lửa, khói. Cần một transform trên cùng entity.
///
/// Module particle của engine sinh và cập nhật hạt trong `phase_post_update`
/// theo delta() (nên dừng khi pause, chậm lại khi time_set_scale()), và vẽ
/// chúng trong `phase_render` cùng với sprite, theo `layer`. Trên cùng một
/// lớp, hạt vẽ sau sprite.
///
/// Hai cách phát:
/// - **Liên tục**: `rate > 0` và `emitting = true`, ví dụ khói từ ống khói.
/// - **Một loạt**: particles_burst(), ví dụ vụ nổ. Dùng particles_spawn() để
///   tạo một entity chỉ để nổ một lần rồi tự hủy.
///
/// Mọi trường đều sửa được lúc đang chạy; hạt đã sinh giữ tốc độ và tuổi thọ
/// của chúng, còn màu và kích thước luôn theo giá trị hiện tại.
struct particle_emitter {
  /// @name Phát
  /// @{
  f32 rate = 0.0f;       ///< Số hạt mỗi giây khi phát liên tục. 0 là chỉ phát theo loạt.
  bool emitting = true;  ///< Đang phát liên tục. Không ảnh hưởng particles_burst().
  i32 max_particles = 512; ///< Số hạt sống tối đa. Hạt mới bị bỏ khi đầy.
  /// Vùng sinh hạt quanh vị trí của entity: hình chữ nhật có tâm tại
  /// `transform.pos`. `{0, 0}` là sinh đúng tại một điểm.
  vec2 area{};
  /// @}

  /// @name Chuyển động
  /// @{
  f32_range life{0.5f, 1.0f};     ///< Tuổi thọ, giây.
  f32_range speed{50.0f, 100.0f}; ///< Tốc độ ban đầu.
  /// Hướng phát, độ (0 là sang phải, 90 là xuống dưới), cộng với góc xoay
  /// của transform.
  f32 angle = 0.0f;
  /// Độ tỏa quanh `angle`, độ. 360 là mọi hướng.
  f32 spread = 360.0f;
  vec2 gravity{};     ///< Gia tốc, đơn vị mỗi giây bình phương. `{0, 400}` là rơi xuống.
  f32 drag = 0.0f;    ///< Lực cản: vận tốc giảm theo tỉ lệ này mỗi giây. 0 là không cản.
  f32_range spin{};   ///< Tốc độ xoay, độ mỗi giây.
  /// Hạt đi theo emitter khi emitter di chuyển (ví dụ lửa ở đuôi tên lửa).
  /// `false` là hạt để lại dấu vết trong thế giới.
  bool local_space = false;
  /// @}

  /// @name Hình ảnh
  /// @{
  f32 size_start = 8.0f; ///< Kích thước lúc sinh, pixel thế giới.
  f32 size_end = 0.0f;   ///< Kích thước lúc chết.
  f32_range size_jitter{1.0f, 1.0f}; ///< Hệ số kích thước ngẫu nhiên riêng của từng hạt.
  rgba color_start{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu lúc sinh.
  rgba color_end{1.0f, 1.0f, 1.0f, 0.0f};   ///< Màu lúc chết. Mặc định mờ dần.
  particle_shape shape = particle_circle; ///< Hình khi không có texture.
  /// Ảnh của hạt. Không hợp lệ thì vẽ `shape`. Ảnh được co giãn cho cạnh lớn
  /// nhất bằng kích thước hạt, rồi nhân màu của hạt.
  texture_handle texture{};
  rect source{};  ///< Vùng trong ảnh, pixel. Kích thước 0 là cả ảnh.
  blend_mode blend = blend_alpha; ///< Cách trộn màu. `blend_additive` cho lửa, tia lửa.
  i32 layer = 0;  ///< Lớp vẽ, cùng quy ước với sprite::layer.
  bool visible = true; ///< Ẩn mà không dừng mô phỏng.
  /// @}

  /// Hủy entity khi không còn phát (`emitting == false` hoặc `rate == 0`),
  /// không còn loạt nào chờ và mọi hạt đã chết. Dùng cho hiệu ứng một lần.
  bool destroy_when_done = false;

  /// @name Trạng thái (do engine quản lý)
  /// @{
  std::vector<particle> particles; ///< Các hạt đang sống.
  i32 pending_burst = 0; ///< Số hạt chờ sinh ở lần cập nhật tới, xem particles_burst().
  f32 emit_accum = 0.0f; ///< Phần lẻ của hạt chưa sinh khi phát liên tục.
  /// @}
};

/// Xếp hàng `count` hạt để sinh cùng lúc ở lần cập nhật tới.
///
/// Không cần `emitting`. Gọi nhiều lần trong một frame thì cộng dồn.
/// @param emitter Emitter.
/// @param count Số hạt.
inline void particles_burst(particle_emitter &emitter, i32 count) {
  if (count > 0)
    emitter.pending_burst += count;
}

/// Tạo một entity tại `pos` chỉ để phát một loạt `count` hạt, theo mẫu
/// `preset`, rồi tự hủy khi hạt cuối cùng chết.
///
/// `preset` thường là một hằng số của game, ví dụ `explosion`. Entity mới được
/// gắn vào scene đang chạy (njin::scene_owned), nên rời scene thì nó cũng mất.
/// @param ctx Context của engine.
/// @param preset Mẫu emitter. `emitting` và `destroy_when_done` bị ghi đè.
/// @param pos Vị trí trong thế giới.
/// @param count Số hạt.
/// @return Entity vừa tạo.
entt::entity particles_spawn(njin_ctx &ctx, const particle_emitter &preset,
                             vec2 pos, i32 count);
/// @}
} // namespace njin
