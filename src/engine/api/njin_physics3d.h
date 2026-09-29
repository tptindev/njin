#pragma once
#include "njin_3d.h"

namespace njin {
struct context;

/// @addtogroup grp_physics3d
/// @{

/// Cách một body vật lý 3D chuyển động.
enum body3d_motion {
  body3d_static,    ///< Đứng yên mãi: sàn, tường, bục cố định. Rẻ nhất.
  body3d_kinematic, ///< Game dời bằng body3d_move_kinematic(): bục di chuyển, cửa. Không bị vật khác đẩy, nhưng chở và đẩy được vật khác.
  body3d_dynamic,   ///< Do vật lý điều khiển: rơi, va, lăn, bị đẩy. Thùng, bóng, mảnh vỡ.
};

/// Mô tả một body vật lý 3D cho body3d_create().
///
/// Hình và kích thước theo cùng quy ước với njin::shape3d, nên một body và hình
/// vẽ nó dùng chung số: `shape`, `size` (hộp), `radius`, `height` (viên nang, trụ,
/// cả hình theo trục y). Hình xuyến (njin::shape3d_torus) không có body.
struct body3d_desc {
  shape3d_kind shape = shape3d_box;  ///< Hình: hộp, cầu, viên nang hoặc trụ.
  vec3 position{0.0f, 0.0f, 0.0f};   ///< Tâm.
  vec3 rotation{0.0f, 0.0f, 0.0f};   ///< Góc xoay, độ, cùng thứ tự với njin::transform3d.
  vec3 size{1.0f, 1.0f, 1.0f};       ///< Kích thước của hộp theo x, y, z.
  f32 radius = 0.5f;                 ///< Bán kính (cầu, viên nang, trụ).
  f32 height = 1.0f;                 ///< Chiều cao cả hình theo y (viên nang, trụ).
  body3d_motion motion = body3d_static; ///< Cách chuyển động.
  f32 mass = 1.0f;                   ///< Khối lượng, kg, chỉ cho body động.
  f32 friction = 0.5f;               ///< Ma sát, 0..1.
  f32 restitution = 0.0f;            ///< Độ nảy, 0 (không nảy) .. 1 (nảy hết).
  /// Số của game gắn vào body (chỉ số trong mảng, id entity...), đọc lại bằng
  /// body3d_user() hoặc từ kết quả physics3d_raycast().
  u64 user = 0;
};

/// Tạo một body vật lý 3D. Engine mô phỏng mọi body ở `phase_fixed_update`,
/// ngay sau các system của game trong phase đó.
/// @param ctx Context của engine.
/// @param desc Mô tả body.
/// @return Handle của body, hoặc không hợp lệ nếu hình không dùng được.
body3d_handle body3d_create(context &ctx, const body3d_desc &desc);

/// Hủy body. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Body cần hủy.
void body3d_destroy(context &ctx, body3d_handle handle);

/// Vị trí và góc xoay hiện tại của body, để vẽ nó (`scale` luôn là 1).
/// @param ctx Context của engine.
/// @param handle Body.
/// @return Vị trí và góc xoay (độ); mặc định nếu handle không hợp lệ.
transform3d body3d_transform(const context &ctx, body3d_handle handle);

/// Dời body tức thời tới vị trí mới (dịch chuyển, không va chạm trên đường đi),
/// ví dụ khi đặt lại màn chơi. Vận tốc giữ nguyên.
/// @param ctx Context của engine.
/// @param handle Body.
/// @param position Vị trí mới.
/// @param rotation Góc xoay mới, độ.
void body3d_set_position(context &ctx, body3d_handle handle, vec3 position, vec3 rotation = {});

/// Cho một body kinematic tới `position` sau bước mô phỏng tới. Engine đặt vận tốc
/// của body sao cho nó tới đó đúng hạn, nên nhân vật và vật động đứng trên được chở theo.
/// Gọi mỗi bước cố định với vị trí mới (bục đi qua lại, thang máy).
/// @param ctx Context của engine.
/// @param handle Body kinematic.
/// @param position Vị trí muốn tới.
/// @param rotation Góc xoay muốn tới, độ.
void body3d_move_kinematic(context &ctx, body3d_handle handle, vec3 position, vec3 rotation = {});

/// Vận tốc thẳng của body, đơn vị mỗi giây.
/// @param ctx Context của engine.
/// @param handle Body.
/// @return Vận tốc, hoặc 0 nếu handle không hợp lệ.
vec3 body3d_velocity(const context &ctx, body3d_handle handle);

/// Đặt vận tốc thẳng của một body động.
/// @param ctx Context của engine.
/// @param handle Body.
/// @param velocity Vận tốc mới, đơn vị mỗi giây.
void body3d_set_velocity(context &ctx, body3d_handle handle, vec3 velocity);

/// Đẩy một body động một cú (xung lực, kg * đơn vị mỗi giây) tại tâm của nó:
/// nổ, cú đá, đạn trúng.
/// @param ctx Context của engine.
/// @param handle Body.
/// @param impulse Xung lực.
void body3d_add_impulse(context &ctx, body3d_handle handle, vec3 impulse);

/// Số của game gắn vào body lúc tạo (njin::body3d_desc::user).
/// @param ctx Context của engine.
/// @param handle Body.
/// @return Số đó, hoặc 0 nếu handle không hợp lệ.
u64 body3d_user(const context &ctx, body3d_handle handle);

/// Mô tả một nhân vật cho character3d_create(): hình viên nang đứng thẳng, đi
/// trên sàn, leo bậc thấp, không trượt trên dốc thoải, bị tường chặn, và đẩy
/// được body động.
struct character3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Vị trí **chân** (đáy viên nang).
  f32 radius = 0.3f;               ///< Bán kính viên nang.
  f32 height = 1.8f;               ///< Chiều cao cả viên nang.
  f32 max_slope = 50.0f;           ///< Dốc nhất còn đứng được, độ.
  f32 step_height = 0.3f;          ///< Bậc cao nhất tự bước lên được.
  f32 mass = 70.0f;                ///< Khối lượng, kg, khi đẩy body động.
};

/// Tạo một nhân vật. Game điều khiển nó bằng vận tốc: mỗi bước cố định gọi
/// character3d_set_velocity() với vận tốc muốn có (cả trọng lực và cú nhảy do
/// game tự cộng), engine di chuyển nó ngay sau đó, trượt dọc tường và dừng trên
/// sàn.
/// @param ctx Context của engine.
/// @param desc Mô tả nhân vật.
/// @return Handle của nhân vật.
character3d_handle character3d_create(context &ctx, const character3d_desc &desc);

/// Hủy nhân vật. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Nhân vật.
void character3d_destroy(context &ctx, character3d_handle handle);

/// Đặt vận tốc muốn có cho bước mô phỏng tới, đơn vị mỗi giây.
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @param velocity Vận tốc.
void character3d_set_velocity(context &ctx, character3d_handle handle, vec3 velocity);

/// Vận tốc sau bước mô phỏng vừa rồi (đã bị tường, sàn chặn bớt).
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @return Vận tốc.
vec3 character3d_velocity(const context &ctx, character3d_handle handle);

/// Vị trí chân của nhân vật.
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @return Vị trí.
vec3 character3d_position(const context &ctx, character3d_handle handle);

/// Dời nhân vật tức thời tới vị trí chân mới (hồi sinh, cổng dịch chuyển).
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @param position Vị trí chân mới.
void character3d_set_position(context &ctx, character3d_handle handle, vec3 position);

/// Nhân vật có đang đứng trên thứ gì đủ bằng phẳng không (sau bước mô phỏng vừa rồi).
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @return `true` nếu đang đứng.
bool character3d_grounded(const context &ctx, character3d_handle handle);

/// Vận tốc của thứ nhân vật đang đứng lên: bằng 0 trên sàn tĩnh, bằng vận tốc của
/// bục trên một body kinematic. Cộng vào vận tốc muốn có để nhân vật đi theo bục.
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @return Vận tốc của chỗ đứng, hoặc 0 nếu không đứng trên gì.
vec3 character3d_ground_velocity(const context &ctx, character3d_handle handle);

/// Body mà nhân vật đang đứng lên (để biết đã tới bục nào).
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @return Body, hoặc không hợp lệ nếu không đứng trên body nào.
body3d_handle character3d_ground_body(const context &ctx, character3d_handle handle);

/// Bắn một tia vào các body (không trúng nhân vật): đạn, tầm nhìn, chọn vật bằng chuột.
/// @param ctx Context của engine.
/// @param ray Tia (hướng độ dài 1).
/// @param max_distance Xa nhất còn tính, đơn vị thế giới.
/// @param body Nếu khác nullptr, nhận body bị trúng (không hợp lệ nếu trượt).
/// @return Điểm chạm gần nhất, nếu có.
ray3d_hit physics3d_raycast(const context &ctx, const ray3d &ray, f32 max_distance,
                            body3d_handle *body = nullptr);

/// Đặt trọng lực cho body động. Mặc định `{0, -9.81, 0}`. Nhân vật không dùng giá
/// trị này: game tự cộng trọng lực vào vận tốc của nó.
/// @param ctx Context của engine.
/// @param gravity Gia tốc, đơn vị mỗi giây bình phương.
void physics3d_set_gravity(context &ctx, vec3 gravity);

/// Trọng lực đang dùng cho body động.
/// @param ctx Context của engine.
/// @return Gia tốc.
vec3 physics3d_gravity(const context &ctx);
/// @}
} // namespace njin
