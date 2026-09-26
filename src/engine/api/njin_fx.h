#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_particles.h"
#include <entt/entity/fwd.hpp>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_fx
/// @{

/// @name Hiệu ứng màn hình và thời gian
/// @{

/// Cách rung của camera_shake(). Đặt một lần bằng camera_shake_config(), hoặc
/// để mặc định.
struct shake_config {
  f32 max_offset = 16.0f; ///< Độ lệch tối đa khi độ rung = 1, pixel màn hình.
  f32 max_angle = 3.0f;   ///< Góc lệch tối đa khi độ rung = 1, độ.
  f32 frequency = 30.0f;  ///< Tốc độ rung, số lần mỗi giây.
  f32 decay = 1.5f;       ///< Độ rung giảm bao nhiêu mỗi giây.
};

/// Rung camera. Cộng dồn: trúng đạn liên tiếp rung mạnh dần.
///
/// Độ rung (0..1) giảm dần theo `shake_config::decay`. Cường độ thật tỉ lệ với
/// bình phương độ rung, nên rung nhỏ gần như không thấy, rung lớn rất mạnh:
/// 0.2 cho bước chân nặng, 0.4 cho trúng đòn, 0.7 trở lên cho vụ nổ. Chỉ ảnh
/// hưởng hình vẽ ra, không đổi transform của camera hay w2scr()/scr2w().
/// Tính theo giờ thật, nên vẫn rung trong hitstop().
/// @param ctx Context của engine.
/// @param trauma Độ rung cộng thêm, giới hạn tổng ở 1.
void camera_shake(njin_ctx &ctx, f32 trauma);

/// Đổi cách rung của camera_shake().
/// @param ctx Context của engine.
/// @param config Cách rung mới.
void camera_shake_config(njin_ctx &ctx, const shake_config &config);

/// Độ rung hiện tại, 0..1.
/// @param ctx Context của engine.
/// @return Độ rung.
f32 camera_shake_amount(const njin_ctx &ctx);

/// Dừng hình trong chốc lát (hitstop, freeze frame) để cú đánh có "lực".
///
/// Trong lúc dừng, delta() trả về 0 và `phase_fixed_update` không chạy, giống
/// time_set_paused(), nhưng tự hết sau `seconds` giây thật. Gọi khi đang dừng
/// thì lấy thời gian dài hơn, không cộng dồn. Thường 0.03 đến 0.12 giây.
/// @param ctx Context của engine.
/// @param seconds Thời gian dừng, giây thật.
void hitstop(njin_ctx &ctx, f32 seconds);

/// Có đang hitstop không.
/// @param ctx Context của engine.
/// @return `true` nếu đang dừng hình.
bool hitstop_active(const njin_ctx &ctx);

/// Nháy cả màn hình một màu rồi mờ dần: trắng khi nổ lớn, đỏ khi bị thương.
///
/// Vẽ đè lên mọi thứ, kể cả UI (nhưng dưới hiệu ứng chuyển scene). Tính theo
/// giờ thật. Gọi lại khi đang nháy thì thay bằng lần nháy mới.
/// @param ctx Context của engine.
/// @param color Màu nháy. `color.a` là độ đục lúc đầu.
/// @param duration Thời gian mờ hết, giây.
void screen_flash(njin_ctx &ctx, rgba color, f32 duration);
/// @}

/// @name Nháy sprite
/// @{

/// Tô một sprite thành một màu trong chốc lát, thường là trắng khi trúng đòn.
///
/// Khác `sprite.tint` (chỉ nhân màu, không làm sáng lên được), nháy này thay
/// màu của từng điểm ảnh mà vẫn giữ hình dáng sprite. Module sprite tự gỡ
/// component khi hết giờ. Dùng sprite_flash() cho gọn.
struct flash_fx {
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu tô. `a` là độ phủ lúc đầu.
  f32 duration = 0.1f; ///< Thời gian nháy, giây.
  f32 time = 0.0f;     ///< Thời gian đã trôi qua, do engine cập nhật.
  bool fade = true;    ///< Mờ dần. `false` là giữ nguyên màu tô đến hết giờ.
};

/// Nháy sprite của `entity`. Gọi lại khi đang nháy thì bắt đầu lại.
/// @param ctx Context của engine.
/// @param entity Entity có sprite.
/// @param color Màu tô.
/// @param duration Thời gian, giây (theo delta(), nên dừng trong hitstop).
void sprite_flash(njin_ctx &ctx, entt::entity entity,
                  rgba color = {1.0f, 1.0f, 1.0f, 1.0f}, f32 duration = 0.1f);
/// @}

/// @name Tan biến sprite
/// @{

/// Cho một sprite tan biến từng mảng nhỏ, có viền cháy sáng ở chỗ đang tan:
/// kẻ địch chết, vật phẩm biến mất. Đặt `reverse` thì ngược lại, sprite hiện ra dần.
///
/// Mỗi mảng có một số ngẫu nhiên cố định (từ hàm băm, không cần ảnh nhiễu); mảng nào có số nhỏ hơn
/// ngưỡng đang chạy từ 0 đến 1 thì biến mất. Hình dáng sprite được giữ nguyên, chỉ bớt mảng.
/// Module sprite cập nhật `time` theo delta() (nên dừng trong hitstop).
///
/// Khi hết giờ:
/// - tan biến (mặc định): sprite **ẩn hẳn** và component ở lại, để nó không hiện lại. Đặt
///   `destroy_when_done` để hủy luôn entity, hoặc tự gỡ component để sprite hiện lại;
/// - `reverse`: component tự được gỡ và sprite hiện đủ.
///
/// Dùng chung được với njin::flash_fx: nháy trắng rồi tan biến.
/// Dùng sprite_dissolve() cho gọn.
struct dissolve_fx {
  rgba edge_color{1.0f, 0.55f, 0.1f, 1.0f}; ///< Màu viền cháy. `a` là độ đậm, 0 là bỏ viền.
  f32 edge_width = 0.08f; ///< Độ dày viền, tính theo thang ngẫu nhiên 0..1. 0 là bỏ viền.
  f32 grain = 2.0f;       ///< Cỡ mỗi mảng, tính bằng pixel của ảnh sprite. 1 là từng pixel, lớn hơn là mảng to.
  f32 seed = 0.0f;        ///< Đổi hình mẫu tan biến. Cho mỗi kẻ địch một giá trị riêng để chúng không tan giống hệt nhau.
  f32 duration = 0.6f;    ///< Thời gian tan hết, giây.
  f32 time = 0.0f;        ///< Thời gian đã trôi qua, do engine cập nhật.
  bool reverse = false;   ///< `true`: hiện ra dần thay vì tan biến.
  bool destroy_when_done = false; ///< Hủy entity khi tan hết. Không áp dụng cho `reverse`.
};

/// Cho sprite của `entity` tan biến. Gọi lại khi đang tan thì bắt đầu lại.
///
/// Muốn chỉnh thêm (hiện ra dần, cỡ mảng, hủy khi xong) thì tự gắn njin::dissolve_fx:
/// @code
/// reg.emplace_or_replace<njin::dissolve_fx>(enemy, njin::dissolve_fx{.duration = 0.8f, .seed = 3.0f,
///                                                                     .destroy_when_done = true});
/// @endcode
/// @param ctx Context của engine.
/// @param entity Entity có sprite.
/// @param duration Thời gian, giây (theo delta(), nên dừng trong hitstop).
/// @param edge_color Màu viền cháy. `a` bằng 0 là không có viền.
void sprite_dissolve(njin_ctx &ctx, entt::entity entity, f32 duration = 0.6f,
                     rgba edge_color = {1.0f, 0.55f, 0.1f, 1.0f});
/// @}

/// Các mẫu particle hay dùng, để truyền vào particles_spawn() hoặc gắn thẳng
/// lên entity. Là giá trị thường: sửa thoải mái trước khi dùng.
///
/// @code
/// njin::particles_spawn(ctx, njin::fx::explosion(), pos, 60);
///
/// njin::particle_emitter smoke = njin::fx::smoke();
/// smoke.color_start = {0.3f, 0.3f, 0.35f, 0.6f};
/// reg.emplace<njin::particle_emitter>(chimney, smoke); // phát liên tục
/// @endcode
namespace fx {
/// Vụ nổ: tỏa mọi hướng, nhanh, vàng cam chuyển đỏ sẫm, sáng (additive).
/// Nổ bằng particles_spawn() với 40–80 hạt.
/// @return Emitter đã cấu hình.
inline particle_emitter explosion() {
  particle_emitter e{};
  e.life = {0.35f, 0.8f};
  e.speed = {120.0f, 420.0f};
  e.drag = 4.0f;
  e.size_start = 18.0f;
  e.size_end = 2.0f;
  e.size_jitter = {0.6f, 1.4f};
  e.color_start = {1.0f, 0.85f, 0.35f, 1.0f};
  e.color_end = {0.7f, 0.1f, 0.05f, 0.0f};
  e.blend = blend_additive;
  e.area = {8.0f, 8.0f};
  return e;
}

/// Tia lửa: nhỏ, rất nhanh, rơi theo trọng lực. Cho kim loại va chạm, đạn
/// trúng tường. Nổ 10–25 hạt; chỉnh `angle`/`spread` để bắn về một phía.
/// @return Emitter đã cấu hình.
inline particle_emitter sparks() {
  particle_emitter e{};
  e.life = {0.2f, 0.5f};
  e.speed = {200.0f, 500.0f};
  e.gravity = {0.0f, 900.0f};
  e.drag = 1.5f;
  e.size_start = 4.0f;
  e.size_end = 1.0f;
  e.color_start = {1.0f, 0.95f, 0.6f, 1.0f};
  e.color_end = {1.0f, 0.45f, 0.1f, 0.0f};
  e.shape = particle_square;
  e.blend = blend_additive;
  return e;
}

/// Bụi: bay lên chậm, xám nâu, mờ dần. Cho bước chạy, tiếp đất, trượt. Nổ
/// 6–12 hạt dưới chân.
/// @return Emitter đã cấu hình.
inline particle_emitter dust() {
  particle_emitter e{};
  e.life = {0.3f, 0.6f};
  e.speed = {20.0f, 70.0f};
  e.angle = -90.0f;
  e.spread = 140.0f;
  e.gravity = {0.0f, -20.0f};
  e.drag = 3.0f;
  e.size_start = 6.0f;
  e.size_end = 12.0f;
  e.size_jitter = {0.7f, 1.3f};
  e.color_start = {0.75f, 0.7f, 0.62f, 0.7f};
  e.color_end = {0.75f, 0.7f, 0.62f, 0.0f};
  e.area = {16.0f, 2.0f};
  return e;
}

/// Khói: bay lên, nở to, mờ dần. Phát liên tục khoảng 15–30 hạt mỗi giây.
/// @return Emitter đã cấu hình.
inline particle_emitter smoke() {
  particle_emitter e{};
  e.rate = 20.0f;
  e.life = {1.2f, 2.2f};
  e.speed = {20.0f, 45.0f};
  e.angle = -90.0f;
  e.spread = 30.0f;
  e.drag = 0.5f;
  e.spin = {-40.0f, 40.0f};
  e.size_start = 10.0f;
  e.size_end = 36.0f;
  e.color_start = {0.55f, 0.55f, 0.58f, 0.5f};
  e.color_end = {0.35f, 0.35f, 0.38f, 0.0f};
  e.area = {6.0f, 2.0f};
  return e;
}

/// Lửa: ngọn lửa phát liên tục, bay lên, vàng chuyển đỏ, sáng (additive).
/// @return Emitter đã cấu hình.
inline particle_emitter fire() {
  particle_emitter e{};
  e.rate = 70.0f;
  e.life = {0.4f, 0.8f};
  e.speed = {50.0f, 120.0f};
  e.angle = -90.0f;
  e.spread = 25.0f;
  e.size_start = 22.0f;
  e.size_end = 2.0f;
  e.size_jitter = {0.7f, 1.2f};
  e.color_start = {1.0f, 0.8f, 0.3f, 0.9f};
  e.color_end = {0.9f, 0.15f, 0.05f, 0.0f};
  e.blend = blend_additive;
  e.area = {14.0f, 4.0f};
  return e;
}

/// Lấp lánh: hạt nhỏ xoay, trôi nhẹ, sáng. Cho nhặt đồ, phép thuật, hồi máu.
/// Nổ 12–20 hạt, hoặc phát liên tục quanh vật phẩm.
/// @return Emitter đã cấu hình.
inline particle_emitter sparkle() {
  particle_emitter e{};
  e.life = {0.4f, 0.9f};
  e.speed = {30.0f, 120.0f};
  e.drag = 2.5f;
  e.gravity = {0.0f, -30.0f};
  e.spin = {-360.0f, 360.0f};
  e.size_start = 7.0f;
  e.size_end = 0.0f;
  e.color_start = {1.0f, 1.0f, 0.75f, 1.0f};
  e.color_end = {0.5f, 0.8f, 1.0f, 0.0f};
  e.shape = particle_square;
  e.blend = blend_additive;
  e.area = {12.0f, 12.0f};
  return e;
}

/// Mảnh vỡ: khối vuông văng lên rồi rơi, xoay. Cho thùng vỡ, đá vỡ. Nổ 8–16
/// hạt; đổi `color_start`/`color_end` theo vật liệu.
/// @return Emitter đã cấu hình.
inline particle_emitter debris() {
  particle_emitter e{};
  e.life = {0.6f, 1.1f};
  e.speed = {120.0f, 320.0f};
  e.angle = -90.0f;
  e.spread = 120.0f;
  e.gravity = {0.0f, 1100.0f};
  e.spin = {-540.0f, 540.0f};
  e.size_start = 7.0f;
  e.size_end = 5.0f;
  e.size_jitter = {0.6f, 1.4f};
  e.color_start = {0.55f, 0.4f, 0.25f, 1.0f};
  e.color_end = {0.45f, 0.32f, 0.2f, 0.0f};
  e.shape = particle_square;
  return e;
}

/// Máu hoặc chất lỏng bắn: giọt đỏ văng ra rồi rơi. Nổ 10–20 hạt, hướng
/// `angle` theo chiều cú đánh.
/// @return Emitter đã cấu hình.
inline particle_emitter splash() {
  particle_emitter e{};
  e.life = {0.35f, 0.7f};
  e.speed = {100.0f, 300.0f};
  e.spread = 70.0f;
  e.gravity = {0.0f, 900.0f};
  e.size_start = 6.0f;
  e.size_end = 2.0f;
  e.size_jitter = {0.6f, 1.3f};
  e.color_start = {0.75f, 0.05f, 0.08f, 1.0f};
  e.color_end = {0.45f, 0.0f, 0.02f, 0.0f};
  return e;
}

/// Mưa: vệt rơi nhanh, phủ một vùng rộng. Gắn lên entity đi theo camera,
/// đặt `area` bằng bề ngang màn hình (và lớn hơn một chút).
/// @return Emitter đã cấu hình.
inline particle_emitter rain() {
  particle_emitter e{};
  e.rate = 250.0f;
  e.max_particles = 1500;
  e.life = {0.6f, 0.9f};
  e.speed = {700.0f, 900.0f};
  e.angle = 100.0f;
  e.spread = 4.0f;
  e.size_start = 3.0f;
  e.size_end = 3.0f;
  e.color_start = {0.7f, 0.8f, 1.0f, 0.55f};
  e.color_end = {0.7f, 0.8f, 1.0f, 0.3f};
  e.shape = particle_square;
  e.area = {1400.0f, 10.0f};
  return e;
}

/// Tuyết: bông trắng rơi chậm, lắc nhẹ. Dùng như rain().
/// @return Emitter đã cấu hình.
inline particle_emitter snow() {
  particle_emitter e{};
  e.rate = 60.0f;
  e.max_particles = 1500;
  e.life = {6.0f, 9.0f};
  e.speed = {30.0f, 70.0f};
  e.angle = 90.0f;
  e.spread = 50.0f;
  e.size_start = 4.0f;
  e.size_end = 4.0f;
  e.size_jitter = {0.5f, 1.3f};
  e.color_start = {1.0f, 1.0f, 1.0f, 0.9f};
  e.color_end = {1.0f, 1.0f, 1.0f, 0.0f};
  e.area = {1400.0f, 10.0f};
  return e;
}

/// Vệt đuôi: hạt để lại phía sau khi entity di chuyển (tên lửa, phi tiêu,
/// sao băng). Phát liên tục, không bay, chỉ nhỏ dần và mờ dần.
/// @return Emitter đã cấu hình.
inline particle_emitter trail() {
  particle_emitter e{};
  e.rate = 80.0f;
  e.life = {0.25f, 0.4f};
  e.speed = {0.0f, 8.0f};
  e.size_start = 8.0f;
  e.size_end = 0.0f;
  e.color_start = {1.0f, 1.0f, 1.0f, 0.8f};
  e.color_end = {0.6f, 0.8f, 1.0f, 0.0f};
  e.blend = blend_additive;
  return e;
}
} // namespace fx
/// @}
} // namespace njin
