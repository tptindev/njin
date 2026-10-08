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
///
/// Có `model` thì hình của body lấy từ lưới tam giác của model đó, đặt như
/// draw_model() vẽ nó với cùng `position`, `rotation` và `scale`.
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
  /// Lấy hình từ model này (model_load()) thay cho `shape`. Body tĩnh và kinematic
  /// dùng đúng từng tam giác: sàn, dốc, hang của một màn làm trong Blender. Body
  /// động dùng bao lồi của các đỉnh (hình lồi nhỏ nhất bọc model).
  model_handle model{};
  vec3 scale{1.0f, 1.0f, 1.0f}; ///< Tỉ lệ của `model`, như njin::transform3d::scale.
  /// Chỉ phát hiện, không va chạm: vật và nhân vật đi xuyên qua, còn engine báo
  /// sự kiện chạm (physics3d_contact()). Cho vùng nhặt đồ, checkpoint, bẫy, đích.
  bool sensor = false;
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

/// Cho bước vật lý kế tiếp, body động mang thêm một vật nặng `mass` kg đặt tại
/// `point`: người treo hay leo trên nó (nhân vật đứng trên body thì engine tự
/// làm), một thùng hàng không phải body. Body nặng như cả hai cộng lại, có quán
/// tính của vật nặng ở chỗ đó và sức nặng của nó xoay body quanh tâm, như nhân
/// vật đứng lên (character3d_desc::mass): ván dựng vào tường trượt ra khi người
/// leo lên cao, mà ván nhẹ không rung. Gọi mỗi bước cố định cho đến khi thôi mang.
/// @param ctx Context của engine.
/// @param handle Body. Không phải body động thì bỏ qua.
/// @param mass Khối lượng, kg. Không dương thì bỏ qua.
/// @param point Chỗ đặt, thế giới; được giữ ở trong body.
void body3d_carry(context &ctx, body3d_handle handle, f32 mass, vec3 point);

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
  /// Khối lượng, kg: sức nặng đè lên body động mà nhân vật đứng lên (bập bênh
  /// nghiêng, ván nằm trên sàn vẫn yên, ván dựng vào tường trượt ra), chia đều
  /// cho mọi điểm đỡ, cả sức nặng dù body nhẹ đến đâu.
  f32 mass = 70.0f;
  /// Hai nhân vật cùng bật `push` không chặn cứng nhau: sau mỗi bước, cặp nào
  /// chồng lên nhau theo phương ngang được tách ra bằng vector tịnh tiến nhỏ
  /// nhất, chia theo khối lượng (người nặng ủi người nhẹ sang bên), nên đám
  /// đông lách qua nhau thay vì khựng lại. Với tường, body và nhân vật không bật
  /// thì vẫn chặn như thường.
  bool push = false;
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

/// Bật hoặc tắt một nhân vật. Nhân vật tắt không được di chuyển, không va vào
/// gì và nhân vật khác đi xuyên qua nó, nhưng vẫn giữ vị trí và đặt lại được
/// bằng character3d_set_position(). Mỗi nhân vật bật tốn một lần tính va chạm mỗi
/// bước vật lý, nên một đám đông lớn chỉ bật những người ở gần camera, còn người
/// ở xa game tự dời theo đường đi (không ai thấy họ va chạm).
///
/// Các nhân vật (bật) chặn nhau: không đi xuyên qua nhau mà trượt vòng qua.
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @param active `true` là bật (mặc định khi tạo).
void character3d_set_active(context &ctx, character3d_handle handle, bool active);

/// Nhân vật có đang bật không (character3d_set_active()).
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @return `true` nếu đang bật; `false` nếu tắt hay handle không hợp lệ.
bool character3d_active(const context &ctx, character3d_handle handle);

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

/// Vật mềm mà nhân vật đang đứng lên (nệm, bạt nhún, softbody3d_desc::walkable).
/// Khi đó character3d_ground_body() không hợp lệ, còn character3d_ground_velocity()
/// là vận tốc của mặt vật mềm dưới chân: cộng vào cú nhảy để bật cao hơn khi
/// bạt đang nảy lên.
/// @param ctx Context của engine.
/// @param handle Nhân vật.
/// @return Vật mềm, hoặc không hợp lệ nếu không đứng trên vật mềm nào.
softbody3d_handle character3d_ground_soft(const context &ctx, character3d_handle handle);

/// Vector đẩy ngắn nhất (minimum translation vector) đưa viên nang `a`–`b` bán
/// kính `radius` ra khỏi mọi body nó đang lấn vào (không tính nhân vật, sensor và
/// `ignore`): dời viên nang theo vector này là vừa hết chạm. Để tay, thân của một
/// nhân vật tự né tường thay vì xuyên qua.
///
/// @code
/// // Cẳng tay lấn vào tường: xoay khuỷu cho bàn tay ra theo vector đẩy.
/// const njin::vec3 push = njin::physics3d_capsule_push(ctx, elbow, wrist, 0.045f);
/// @endcode
/// @param ctx Context của engine.
/// @param a Một đầu trục viên nang (tâm nửa cầu), thế giới.
/// @param b Đầu kia.
/// @param radius Bán kính.
/// @param ignore Body không tính (vật nhân vật đang cầm). Không hợp lệ là tính hết.
/// @return Vector đẩy, `{0, 0, 0}` nếu không lấn vào gì.
vec3 physics3d_capsule_push(const context &ctx, vec3 a, vec3 b, f32 radius, body3d_handle ignore = {});

/// Như physics3d_capsule_push() cho một hộp (bàn chân, bàn tay đo từ da bằng
/// model_bone_bounds()).
/// @param ctx Context của engine.
/// @param center Tâm hộp, thế giới.
/// @param rotation Góc xoay, độ, cùng thứ tự với njin::transform3d.
/// @param size Kích thước theo x, y, z của hộp.
/// @param ignore Body không tính. Không hợp lệ là tính hết.
/// @return Vector đẩy, `{0, 0, 0}` nếu không lấn vào gì.
vec3 physics3d_box_push(const context &ctx, vec3 center, vec3 rotation, vec3 size, body3d_handle ignore = {});

/// Đẩy một hộp đi theo `motion` và tìm thứ đầu tiên nó chạm (không tính nhân
/// vật, sensor, `ignore` và những gì hộp đã lấn vào từ đầu). Như physics3d_raycast()
/// nhưng cho cả một khối: thả bàn chân xuống để tìm chỗ đặt trên mặt gồ ghề.
///
/// @code
/// // Bàn chân rơi xuống tối đa 1 m: chạm ở đâu thì đặt ở đó.
/// const njin::ray3d_hit h = njin::physics3d_box_cast(ctx, foot, rot, size, {0, -1, 0});
/// @endcode
/// @param ctx Context của engine.
/// @param center Tâm hộp lúc bắt đầu, thế giới.
/// @param rotation Góc xoay, độ, cùng thứ tự với njin::transform3d.
/// @param size Kích thước theo x, y, z của hộp.
/// @param motion Hướng và quãng đường đẩy.
/// @param ignore Body không tính. Không hợp lệ là tính hết.
/// @param body Nếu khác nullptr, nhận body bị chạm.
/// @return `distance`: quãng hộp đi được tới lúc chạm; `point`, `normal`: chỗ chạm
/// và pháp tuyến của mặt bị chạm.
ray3d_hit physics3d_box_cast(const context &ctx, vec3 center, vec3 rotation, vec3 size, vec3 motion,
                             body3d_handle ignore = {}, body3d_handle *body = nullptr);

/// Chỗ một tia hay một khối đẩy trúng vật mềm, cho các bản physics3d_raycast(),
/// physics3d_box_cast() và physics3d_hull_cast() có tham số `soft`.
struct soft3d_hit {
  softbody3d_handle soft{}; ///< Vật mềm bị trúng; không hợp lệ nếu trúng body hay không trúng gì.
  i32 face = -1;            ///< Tam giác bị trúng: tam giác thứ `face` của softbody3d_indices() (chỉ số `3 * face`).
  i32 vertex = -1;          ///< Đỉnh của tam giác đó gần chỗ trúng nhất: để ghim, kéo, hay đẩy chỗ bị bắn.
};

/// Như physics3d_box_cast(), nhưng hộp chạm cả vật mềm (rèm, vải, bóng), không
/// chỉ body. Chạm vật mềm trước thì `*body` không hợp lệ và `*soft` cho biết vật mềm
/// nào, tam giác nào; chạm body trước thì `soft->soft` không hợp lệ.
/// @param ctx Context của engine.
/// @param center Tâm hộp lúc bắt đầu, thế giới.
/// @param rotation Góc xoay, độ, cùng thứ tự với njin::transform3d.
/// @param size Kích thước theo x, y, z của hộp.
/// @param motion Hướng và quãng đường đẩy.
/// @param ignore Body không tính. Không hợp lệ là tính hết.
/// @param body Nếu khác nullptr, nhận body bị chạm.
/// @param soft Nhận vật mềm bị chạm; nullptr thì bỏ qua vật mềm, như bản không có tham số này.
/// @return Như physics3d_box_cast().
ray3d_hit physics3d_box_cast(const context &ctx, vec3 center, vec3 rotation, vec3 size, vec3 motion,
                             body3d_handle ignore, body3d_handle *body, soft3d_hit *soft);

/// Dựng một khối lồi từ các điểm (khối nhỏ nhất bọc hết chúng), để dò va chạm
/// bằng đúng hình của một bộ phận: bàn chân, bàn tay lấy từ model_bone_points().
/// Khối không phải là body: nó không va chạm, chỉ dùng cho physics3d_hull_push()
/// và physics3d_hull_cast(). Dựng một lần, dùng mỗi khung ở vị trí và góc khác nhau.
///
/// @code
/// std::vector<njin::vec3> pts(njin::model_bone_points(ctx, man, foot_l, false, nullptr, 0));
/// njin::model_bone_points(ctx, man, foot_l, false, pts.data(), (njin::i32)pts.size());
/// const njin::hull3d_handle sole = njin::physics3d_hull_create(ctx, pts.data(), (njin::i32)pts.size());
/// @endcode
/// @param ctx Context của engine.
/// @param points Các điểm, trong hệ trục riêng của khối.
/// @param count Số điểm (ít nhất 4, không cùng nằm trên một mặt phẳng).
/// @return Handle, hoặc không hợp lệ nếu các điểm không dựng được khối.
hull3d_handle physics3d_hull_create(context &ctx, const vec3 *points, i32 count);

/// Hủy khối. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param hull Khối.
void physics3d_hull_destroy(context &ctx, hull3d_handle hull);

/// Như physics3d_box_push() cho một khối lồi đặt ở `position`, xoay `rotation`.
/// @param ctx Context của engine.
/// @param hull Khối.
/// @param position Gốc hệ trục riêng của khối, thế giới.
/// @param rotation Góc xoay, độ, cùng thứ tự với njin::transform3d.
/// @param ignore Body không tính. Không hợp lệ là tính hết.
/// @return Vector đẩy, `{0, 0, 0}` nếu không lấn vào gì hay handle không hợp lệ.
vec3 physics3d_hull_push(const context &ctx, hull3d_handle hull, vec3 position, vec3 rotation, body3d_handle ignore = {});

/// Như physics3d_box_cast() cho một khối lồi.
/// @param ctx Context của engine.
/// @param hull Khối.
/// @param position Gốc hệ trục riêng của khối lúc bắt đầu, thế giới.
/// @param rotation Góc xoay, độ, cùng thứ tự với njin::transform3d.
/// @param motion Hướng và quãng đường đẩy.
/// @param ignore Body không tính. Không hợp lệ là tính hết.
/// @param body Nếu khác nullptr, nhận body bị chạm.
/// @return Như physics3d_box_cast(); không chạm gì nếu handle không hợp lệ.
ray3d_hit physics3d_hull_cast(const context &ctx, hull3d_handle hull, vec3 position, vec3 rotation, vec3 motion,
                              body3d_handle ignore = {}, body3d_handle *body = nullptr);

/// Như physics3d_hull_cast(), chạm cả vật mềm như bản physics3d_box_cast() có `soft`.
/// @param ctx Context của engine.
/// @param hull Khối.
/// @param position Gốc hệ trục riêng của khối lúc bắt đầu, thế giới.
/// @param rotation Góc xoay, độ, cùng thứ tự với njin::transform3d.
/// @param motion Hướng và quãng đường đẩy.
/// @param ignore Body không tính. Không hợp lệ là tính hết.
/// @param body Nếu khác nullptr, nhận body bị chạm.
/// @param soft Nhận vật mềm bị chạm; nullptr thì bỏ qua vật mềm.
/// @return Như physics3d_box_cast(); không chạm gì nếu handle không hợp lệ.
ray3d_hit physics3d_hull_cast(const context &ctx, hull3d_handle hull, vec3 position, vec3 rotation, vec3 motion,
                              body3d_handle ignore, body3d_handle *body, soft3d_hit *soft);

/// Các cạnh của khối, từng cặp điểm trong hệ trục riêng của nó, để vẽ debug.
/// @param ctx Context của engine.
/// @param hull Khối.
/// @param out Mảng nhận điểm (cạnh thứ i là `out[2i]`, `out[2i + 1]`), hoặc nullptr để chỉ đếm.
/// @param count Số phần tử của `out`.
/// @return Số điểm có (gấp đôi số cạnh).
i32 physics3d_hull_lines(const context &ctx, hull3d_handle hull, vec3 *out, i32 count);

/// Bắn một tia vào các body (không trúng nhân vật, đi xuyên sensor): đạn, tầm nhìn,
/// chọn vật bằng chuột.
/// @param ctx Context của engine.
/// @param ray Tia (hướng độ dài 1).
/// @param max_distance Xa nhất còn tính, đơn vị thế giới.
/// @param body Nếu khác nullptr, nhận body bị trúng (không hợp lệ nếu trượt).
/// @return Điểm chạm gần nhất, nếu có.
ray3d_hit physics3d_raycast(const context &ctx, const ray3d &ray, f32 max_distance,
                            body3d_handle *body = nullptr);

/// Như physics3d_raycast(), nhưng tia trúng cả vật mềm: bắn vào tấm rèm thì trúng
/// rèm, không đi xuyên qua. Trúng vật mềm trước thì `*body` không hợp lệ và `*soft`
/// cho biết vật mềm, tam giác và đỉnh gần nhất (để đẩy chỗ bị bắn bằng
/// softbody3d_add_impulse() hay ghim nó); trúng body trước thì `soft->soft` không hợp lệ.
///
/// @code
/// njin::body3d_handle body;
/// njin::soft3d_hit soft;
/// const njin::ray3d_hit hit = njin::physics3d_raycast(ctx, shot, 100.0f, &body, &soft);
/// if (hit.hit && soft.soft.id != 0)
///   njin::softbody3d_add_impulse(ctx, soft.soft, shot.direction * 2.0f);
/// @endcode
/// @param ctx Context của engine.
/// @param ray Tia (hướng độ dài 1).
/// @param max_distance Xa nhất còn tính, đơn vị thế giới.
/// @param body Nếu khác nullptr, nhận body bị trúng (không hợp lệ nếu trượt hay trúng vật mềm).
/// @param soft Nhận vật mềm bị trúng; nullptr thì bỏ qua vật mềm, như bản không có tham số này.
/// @return Điểm chạm gần nhất, nếu có.
ray3d_hit physics3d_raycast(const context &ctx, const ray3d &ray, f32 max_distance, body3d_handle *body,
                            soft3d_hit *soft);

/// Một sự kiện chạm của bước mô phỏng vừa rồi: hai body, hoặc một body và một
/// nhân vật, bắt đầu hay thôi chạm nhau.
struct contact3d {
  body3d_handle a{};              ///< Body thứ nhất.
  body3d_handle b{};              ///< Body thứ hai; không hợp lệ khi bên kia là nhân vật.
  character3d_handle character{}; ///< Nhân vật chạm `a`, khi `b` không hợp lệ.
  bool began = true;              ///< `true`: bắt đầu chạm. `false`: thôi chạm.
  bool sensor = false;            ///< Một trong hai là sensor (njin::body3d_desc::sensor).
  vec3 point{0.0f, 0.0f, 0.0f};   ///< Điểm chạm, khi `began`.
  /// Pháp tuyến chạm, khi `began`: từ `a` sang `b` (hoặc sang nhân vật).
  vec3 normal{0.0f, 0.0f, 0.0f};
};

/// Số sự kiện chạm của bước mô phỏng vừa rồi.
///
/// Một cặp chỉ báo một lần khi bắt đầu chạm và một lần khi thôi chạm, dù chạm ở
/// nhiều điểm. Engine mô phỏng ngay sau các system của game trong
/// `phase_fixed_update`, nên đọc sự kiện trong phase đó: mỗi bước game thấy đúng
/// sự kiện của bước trước, không sót, không lặp. Body bị hủy không có sự kiện
/// thôi chạm.
///
/// @code
/// for (int i = 0; i < njin::physics3d_contact_count(ctx); i++) {
///   const njin::contact3d c = njin::physics3d_contact(ctx, i);
///   if (c.began && c.a.id == goal.id && c.character.id == player.id)
///     win();
/// }
/// @endcode
/// @param ctx Context của engine.
/// @return Số sự kiện.
i32 physics3d_contact_count(const context &ctx);

/// Sự kiện chạm thứ `index` của bước mô phỏng vừa rồi.
/// @param ctx Context của engine.
/// @param index 0..physics3d_contact_count() - 1.
/// @return Sự kiện, hoặc mặc định nếu `index` không hợp lệ.
contact3d physics3d_contact(const context &ctx, i32 index);

/// Loại khớp nối của njin::joint3d_desc.
enum joint3d_kind {
  joint3d_fixed,    ///< Hàn cứng hai body: giữ nguyên vị trí và góc tương đối.
  joint3d_point,    ///< Khớp cầu: xoay tự do quanh `anchor` (dây xích, ragdoll).
  joint3d_hinge,    ///< Bản lề: xoay quanh `axis` qua `anchor` (cửa, bập bênh, bánh xe).
  joint3d_slider,   ///< Trượt dọc `axis`, không xoay (piston, ngăn kéo, cửa kéo).
  joint3d_distance, ///< Giữ khoảng cách giữa `anchor` và `anchor_b` trong `[min, max]` (dây, thanh nối).
};

/// Mô tả một khớp nối cho joint3d_create(). Các điểm và trục tính trong tọa độ
/// thế giới, lúc tạo khớp.
struct joint3d_desc {
  joint3d_kind kind = joint3d_hinge; ///< Loại khớp.
  body3d_handle a{};                 ///< Body thứ nhất.
  /// Body thứ hai. Không hợp lệ là nối `a` vào một điểm cố định của thế giới.
  body3d_handle b{};
  vec3 anchor{0.0f, 0.0f, 0.0f};     ///< Điểm nối.
  vec3 anchor_b{0.0f, 0.0f, 0.0f};   ///< Điểm nối ở phía `b`, chỉ cho njin::joint3d_distance.
  vec3 axis{0.0f, 1.0f, 0.0f};       ///< Trục xoay (bản lề) hoặc trục trượt (khớp trượt).
  /// Giới hạn: góc, độ (bản lề); quãng trượt, đơn vị thế giới, 0 là vị trí lúc tạo
  /// (khớp trượt); khoảng cách (njin::joint3d_distance, cả hai bằng 0 là giữ
  /// khoảng cách lúc tạo). Bản lề và khớp trượt cần `min <= 0 <= max` (góc của bản
  /// lề trong -180..180): giá trị ngoài khoảng bị kẹp lại. `min >= max` là không
  /// giới hạn, trừ njin::joint3d_distance.
  f32 min = 0.0f;
  f32 max = 0.0f; ///< Xem `min`.
  /// Mô-tơ của bản lề và khớp trượt: lực tối đa (bản lề: mô-men, N·m) để giữ tốc
  /// độ đặt bằng joint3d_set_motor(). 0 là không có mô-tơ.
  f32 motor_force = 0.0f;
};

/// Nối hai body (hoặc một body với thế giới) bằng một khớp.
///
/// @code
/// // Tấm ván treo bằng bản lề ở mép trên, đung đưa khi nhân vật nhảy lên.
/// const auto plank = njin::body3d_create(ctx, {.position = {0, 4, 0}, .size = {3, 0.2f, 1},
///                                              .motion = njin::body3d_dynamic, .mass = 20});
/// njin::joint3d_create(ctx, {.kind = njin::joint3d_hinge, .a = plank, .anchor = {0, 6, 0}, .axis = {1, 0, 0}});
/// @endcode
/// @param ctx Context của engine.
/// @param desc Mô tả khớp.
/// @return Handle của khớp, hoặc không hợp lệ nếu `a` không hợp lệ.
joint3d_handle joint3d_create(context &ctx, const joint3d_desc &desc);

/// Hủy khớp. Hủy một body cũng hủy mọi khớp của nó. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Khớp.
void joint3d_destroy(context &ctx, joint3d_handle handle);

/// Đặt tốc độ mô-tơ của một bản lề (độ mỗi giây) hay khớp trượt (đơn vị mỗi
/// giây), trong giới hạn `joint3d_desc::motor_force`. 0 là đứng lại và giữ yên.
/// @param ctx Context của engine.
/// @param handle Khớp có `motor_force` lớn hơn 0.
/// @param speed Tốc độ.
void joint3d_set_motor(context &ctx, joint3d_handle handle, f32 speed);

/// Góc hiện tại của bản lề (độ) hoặc quãng trượt của khớp trượt (đơn vị), tính
/// từ lúc tạo khớp.
/// @param ctx Context của engine.
/// @param handle Khớp.
/// @return Giá trị, 0 với loại khớp khác hay handle không hợp lệ.
f32 joint3d_position(const context &ctx, joint3d_handle handle);

/// Một phần của ragdoll: một xương của model thành một viên nang vật lý dọc theo
/// trục y của xương (hướng của xương với rig của Blender). Mặc định viên nang
/// bọc lớp da của phần đó ở tư thế gốc: các đỉnh mà xương này, hay một xương
/// không có phần riêng phía dưới nó (ngón tay, xương đòn), kéo mạnh nhất.
/// Kích thước tính bằng đơn vị thế giới. Góc giới hạn tính từ tư thế gốc trong
/// file (tư thế T hay A), không phải tư thế lúc tạo ragdoll.
struct ragdoll3d_bone {
  const char *name = nullptr; ///< Tên xương (model_bone_find()).
  /// Bán kính viên nang. 0 là đo từ da (bọc 80% số đỉnh); model không có da
  /// thì 0.06.
  f32 radius = 0.0f;
  /// Chiều dài từ gốc xương. 0 là đo từ da (viên nang dài và lệch tâm theo da);
  /// model không có da thì tới xương con xa nhất, xương không có con phía trước
  /// thì thành hình cầu.
  f32 length = 0.0f;
  /// Khớp cầu nối với phần cha: góc lệch tối đa của trục xương, độ.
  f32 swing = 30.0f;
  f32 twist = 15.0f; ///< Góc vặn tối đa quanh trục xương, độ, cả hai chiều.
  /// Bản lề thay cho khớp cầu (gối, khuỷu tay): xoay quanh trục x của xương,
  /// góc trong `[bend_min, bend_max]`, độ, trong -180..180 và chứa 0. Góc dương
  /// đưa trục y của xương về phía trục z của nó (với mannequin của Quaternius,
  /// gối và khuỷu gập về phía dương). `bend_min >= bend_max` là khớp cầu.
  f32 bend_min = 0.0f;
  f32 bend_max = 0.0f; ///< Xem `bend_min`.
};

/// Mô tả một ragdoll cho ragdoll3d_create().
struct ragdoll3d_desc {
  model_handle model{};     ///< Model có xương (model_load()).
  /// Chỗ model đang được vẽ, như draw_model_anim(). Tỉ lệ phải đều ba trục.
  transform3d transform{};
  model_pose pose{};        ///< Tư thế lúc bắt đầu, thường là tư thế vừa vẽ.
  /// Các phần, mỗi phần một xương khác nhau. Mỗi phần nối vào phần có xương gần
  /// nhất phía trên nó trong bộ xương; đúng một phần không có phần nào phía trên
  /// (gốc, thường là hông).
  const ragdoll3d_bone *bones = nullptr;
  u32 bone_count = 0;       ///< Số phần trong `bones`.
  f32 mass = 70.0f;         ///< Khối lượng cả người, kg, chia cho các phần theo thể tích.
  f32 friction = 0.6f;      ///< Ma sát, 0..1.
  vec3 velocity{0.0f, 0.0f, 0.0f}; ///< Vận tốc ban đầu của mọi phần.
  u64 user = 0;             ///< body3d_user() của mọi phần.
};

/// Biến một model có xương thành ragdoll: mỗi phần là một body động, nối với
/// phần cha bằng khớp có giới hạn góc, rơi và va chạm như mọi body. Các phần
/// của cùng một ragdoll không bao giờ va vào nhau, chỉ va với thế giới. Các xương không có phần (ngón tay, đầu ngón chân, gốc) đi theo
/// phần gần nhất phía trên chúng.
///
/// @code
/// // Mannequin của Quaternius ngã xuống từ tư thế đang vẽ.
/// const njin::ragdoll3d_bone parts[] = {
///   {.name = "pelvis"},
///   {.name = "spine_02", .swing = 20, .twist = 15},
///   {.name = "Head", .swing = 40, .twist = 40},
///   {.name = "upperarm_l", .swing = 70, .twist = 30},
///   {.name = "lowerarm_l", .bend_min = 0, .bend_max = 140},
///   {.name = "thigh_l", .swing = 50, .twist = 15},
///   {.name = "calf_l", .bend_min = 0, .bend_max = 140},
///   // ... bên phải như bên trái
/// };
/// rag = njin::ragdoll3d_create(ctx, {.model = man, .transform = at, .pose = pose,
///                                    .bones = parts, .bone_count = std::size(parts)});
/// @endcode
/// @param ctx Context của engine.
/// @param desc Mô tả ragdoll.
/// @return Handle, hoặc không hợp lệ (có cảnh báo trong log) nếu model không có
/// xương, có tên xương không tìm thấy hay trùng nhau, hoặc các phần không nối
/// thành một cây.
ragdoll3d_handle ragdoll3d_create(context &ctx, const ragdoll3d_desc &desc);

/// Hủy ragdoll cùng các body của nó. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Ragdoll.
void ragdoll3d_destroy(context &ctx, ragdoll3d_handle handle);

/// Body của một phần, để đẩy (body3d_add_impulse()), đọc vị trí, hay nhận ra
/// phần bị trúng trong physics3d_raycast() và physics3d_contact(). Không hủy nó
/// bằng body3d_destroy(): hủy cả ragdoll.
/// @param ctx Context của engine.
/// @param handle Ragdoll.
/// @param part Chỉ số trong `ragdoll3d_desc::bones`.
/// @return Body, hoặc không hợp lệ nếu handle hay `part` không hợp lệ.
body3d_handle ragdoll3d_body(const context &ctx, ragdoll3d_handle handle, i32 part);

/// Hình va chạm của một phần, trong thế giới: viên nang (hoặc hình cầu) mà
/// engine đo từ da, đặt theo body của phần đó lúc này. Để vẽ debug, ví dụ
/// bằng gizmo hay draw_shape3d().
/// @param ctx Context của engine.
/// @param handle Ragdoll.
/// @param part Chỉ số trong `ragdoll3d_desc::bones`.
/// @return Hình (njin::shape3d_capsule hoặc njin::shape3d_sphere), hoặc
/// `radius` 0 nếu handle hay `part` không hợp lệ.
shape3d ragdoll3d_shape(const context &ctx, ragdoll3d_handle handle, i32 part);

/// Tư thế hiện tại của ragdoll, để vẽ model bằng `model_pose::bones` ở
/// `transform` (thường là `ragdoll3d_desc::transform`).
///
/// @code
/// static njin::bone_pose3d bones[128];
/// njin::ragdoll3d_bones(ctx, rag, at, bones, 128);
/// njin::draw_model_anim(ctx, man, at, {.bones = bones});
/// @endcode
/// @param ctx Context của engine.
/// @param handle Ragdoll.
/// @param transform Chỗ model sẽ được vẽ.
/// @param out Mảng nhận tư thế, model_bone_count() phần tử.
/// @param count Số phần tử của `out`.
/// @return Số xương đã ghi (model_bone_count()), 0 nếu handle không hợp lệ hay
/// `count` nhỏ hơn số xương.
i32 ragdoll3d_bones(const context &ctx, ragdoll3d_handle handle, const transform3d &transform, bone_pose3d *out,
                    i32 count);

/// Hình dựng sẵn của một vật mềm (njin::softbody3d_desc::kind).
enum softbody3d_kind {
  /// Khối hộp đặc `size`: một lưới điểm đều bên trong hộp, giữ thể tích. Nệm,
  /// khối thạch, khối cao su.
  softbody3d_box,
  /// Mặt cầu bán kính `radius`, rỗng: thêm `pressure` để thành quả bóng.
  softbody3d_sphere,
  /// Mặt của một lưới tam giác (`mesh`, hoặc mọi lưới của `model`), rỗng như
  /// mặt cầu: lưới kín và đặt `pressure` để giữ phồng. Các đỉnh trùng vị trí
  /// (đường nối UV của file) được gộp làm một.
  softbody3d_mesh,
};

/// Mô tả một vật mềm cho softbody3d_create(). Vật mềm là một đám điểm nối bằng
/// lò xo, mỗi bước vật lý biến dạng theo trọng lực, va chạm và áp suất bên trong.
///
/// Đỉnh của vật mềm đánh số theo thứ tự engine dựng: với `softbody3d_mesh` là
/// thứ tự đỉnh của lưới (lưới không có đỉnh trùng vị trí thì giữ nguyên chỉ số),
/// còn với hộp và cầu thì tìm bằng softbody3d_nearest().
struct softbody3d_desc {
  softbody3d_kind kind = softbody3d_sphere; ///< Hình.
  vec3 position{0.0f, 0.0f, 0.0f};  ///< Tâm.
  vec3 rotation{0.0f, 0.0f, 0.0f};  ///< Góc xoay, độ, cùng thứ tự với njin::transform3d.
  vec3 size{1.0f, 1.0f, 1.0f};      ///< Kích thước của hộp theo x, y, z.
  f32 radius = 0.5f;                ///< Bán kính của mặt cầu.
  /// Độ mịn. Hộp: số điểm trên cạnh dài nhất (2..16), các cạnh khác theo cùng
  /// khoảng cách. Cầu: số lần chia nhỏ khối 20 mặt (1..4: 42, 162, 642, 2562
  /// đỉnh). 0 là mặc định: hộp 5 điểm, cầu chia 2 lần.
  i32 detail = 0;
  /// Lưới cho `softbody3d_mesh`, trong hệ trục riêng của vật (đặt ở `position`,
  /// xoay `rotation`). Tam giác ngược chiều kim đồng hồ nhìn từ ngoài vào.
  mesh3d_data mesh{};
  /// Lấy lưới từ model này (model_load()) cho `softbody3d_mesh`, thay cho `mesh`,
  /// đặt như draw_model() vẽ nó với cùng `position`, `rotation` và `scale`.
  model_handle model{};
  vec3 scale{1.0f, 1.0f, 1.0f};     ///< Tỉ lệ của `model`.
  f32 mass = 1.0f;                  ///< Khối lượng cả vật, kg, chia đều cho các đỉnh.
  /// Độ cứng của các cạnh, 0 (dãn như cao su non) .. 1 (không dãn).
  f32 stiffness = 0.9f;
  /// Độ cứng khi uốn, 0 (gập tự do) .. 1 (giữ dáng cong lúc tạo). Hộp không dùng.
  f32 bend = 0.5f;
  /// Áp suất bên trong khi vật ở đúng hình lúc tạo, Pa (N/m²). Bóp nhỏ lại
  /// thì áp suất tăng, như bóng bay. 0 là không có (vật rỗng xẹp dần). Quả bóng
  /// 1 kg bán kính 0.5: khoảng 50 (mềm, lún khi chạm đất) đến 300 (căng); lớn
  /// hơn nữa thì các cạnh dãn ra và vật phồng to hơn lúc tạo. Cần một mặt kín.
  f32 pressure = 0.0f;
  f32 friction = 0.5f;              ///< Ma sát, 0..1.
  f32 restitution = 0.0f;           ///< Độ nảy, 0..1.
  f32 damping = 0.1f;               ///< Cản chuyển động: vận tốc giảm theo tỉ lệ này mỗi giây.
  /// Hệ số cản của không khí trên mặt của vật (gió, softbody3d_set_wind()).
  /// 0 là không khí không tác động.
  f32 drag = 1.0f;
  /// Các đỉnh bị ghim: đứng yên, hoặc theo softbody3d_move_pinned(). Ghim và bỏ
  /// ghim lúc chạy bằng softbody3d_pin().
  const u32 *pinned = nullptr;
  u32 pinned_count = 0;             ///< Số phần tử của `pinned`.
  u64 user = 0;                     ///< Số của game, đọc lại bằng softbody3d_user().
  /// Va chạm với các vật mềm khác cũng bật cờ này: hai tấm vải chồng lên nhau,
  /// bóng rơi lên nệm. Mỗi đỉnh giữ cách mặt của vật kia một khoảng bằng độ dày
  /// của hai vật. `false` là đi xuyên qua vật mềm khác (rẻ hơn, khi chúng không
  /// bao giờ gặp nhau).
  bool collide_soft = true;
  /// Nhân vật đứng được trên mặt trên của nó (nệm, bạt nhún, khối thạch), lún
  /// xuống theo sức nặng của nhân vật (njin::character3d_desc::mass). Mặt dựng
  /// đứng (rèm) thì nhân vật vẫn đi xuyên, đẩy các đỉnh sang bên. `false` là nhân
  /// vật không bao giờ đứng lên nó.
  bool walkable = true;
};

/// Tạo một vật mềm. Vật mềm va chạm với mọi body (rơi lên sàn, quấn quanh
/// thùng) và với nhau (njin::softbody3d_desc::collide_soft). Nhân vật
/// (character3d_create()) đứng được trên mặt trên của nó, còn mặt đứng thì nhân
/// vật đẩy các đỉnh sang bên khi đi qua, nên một tấm rèm không chặn đường.
/// physics3d_raycast() và các hàm dò `physics3d_*_push`, `_cast` đi xuyên qua vật
/// mềm; các bản có tham số njin::soft3d_hit thì trúng nó.
///
/// @code
/// // Quả bóng nảy: mặt cầu có áp suất, rơi từ độ cao 3 m.
/// const njin::softbody3d_handle ball = njin::softbody3d_create(
///     ctx, {.kind = njin::softbody3d_sphere, .position = {0, 3, 0}, .radius = 0.5f, .pressure = 200});
/// // Mỗi khung, giữa begin_3d() và end_3d():
/// njin::draw_model(ctx, njin::softbody3d_model(ctx, ball), {});
/// @endcode
/// @param ctx Context của engine.
/// @param desc Mô tả vật mềm.
/// @return Handle, hoặc không hợp lệ (có cảnh báo trong log) nếu lưới không dùng được.
softbody3d_handle softbody3d_create(context &ctx, const softbody3d_desc &desc);

/// Các cạnh của tấm vải, cộng lại được: `cloth3d_top | cloth3d_left`.
enum cloth3d_edge : u8 {
  cloth3d_top = 1,    ///< Hàng đầu (hàng 0).
  cloth3d_bottom = 2, ///< Hàng cuối.
  cloth3d_left = 4,   ///< Cột đầu (cột 0).
  cloth3d_right = 8,  ///< Cột cuối.
};

/// Mô tả một tấm vải cho cloth3d_create(): một lưới chữ nhật `columns` x `rows`
/// ô, nằm trong mặt phẳng x–y của nó (đứng thẳng như lá cờ, rèm cửa), tâm ở
/// `position`. Đỉnh ở hàng `r`, cột `c` có chỉ số `r * (columns + 1) + c`; hàng 0
/// là cạnh trên, cột 0 là cạnh trái (phía -x). Xoay `rotation.x = 90` để vải nằm
/// ngang (khăn trải bàn).
struct cloth3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Tâm tấm vải.
  vec3 rotation{0.0f, 0.0f, 0.0f}; ///< Góc xoay, độ, cùng thứ tự với njin::transform3d.
  vec2 size{2.0f, 2.0f};           ///< Chiều rộng (x) và chiều cao (y).
  i32 columns = 20;                ///< Số ô theo chiều rộng, 1..512.
  i32 rows = 20;                   ///< Số ô theo chiều cao, 1..512.
  f32 mass = 0.5f;                 ///< Khối lượng cả tấm, kg.
  f32 stiffness = 1.0f;            ///< Độ cứng của sợi vải, 0 (dãn) .. 1 (không dãn).
  f32 bend = 0.05f;                ///< Độ cứng khi gập, 0 (lụa) .. 1 (bìa cứng).
  f32 damping = 0.1f;              ///< Cản chuyển động: vận tốc giảm theo tỉ lệ này mỗi giây.
  f32 friction = 0.5f;             ///< Ma sát, 0..1.
  f32 drag = 1.0f;                 ///< Hệ số cản của không khí, như njin::softbody3d_desc::drag.
  /// Độ dày, đơn vị thế giới: các đỉnh giữ cách mặt vật khác chừng này, để vải
  /// không chìm vào bàn khi vẽ.
  f32 thickness = 0.02f;
  u8 pin_edges = 0;                ///< Các cạnh bị ghim (bit njin::cloth3d_edge).
  const u32 *pinned = nullptr;     ///< Các đỉnh bị ghim thêm (góc trên của lá cờ...).
  u32 pinned_count = 0;            ///< Số phần tử của `pinned`.
  u64 user = 0;                    ///< Số của game, đọc lại bằng softbody3d_user().
  bool collide_soft = true;        ///< Như njin::softbody3d_desc::collide_soft: hai tấm vải không xuyên qua nhau.
  bool walkable = true;            ///< Như njin::softbody3d_desc::walkable: đứng được trên tấm vải nằm ngang.
};

/// Tạo một tấm vải: lá cờ, rèm, áo choàng, khăn trải bàn. Là một vật mềm như
/// softbody3d_create() tạo ra, dùng chung các hàm `softbody3d_*`. Vẽ được từ cả
/// hai mặt.
///
/// @code
/// // Lá cờ ghim cạnh trái vào cột, bay theo gió.
/// const njin::softbody3d_handle flag = njin::cloth3d_create(
///     ctx, {.position = {1, 4, 0}, .size = {2, 1.2f}, .columns = 20, .rows = 12, .pin_edges = njin::cloth3d_left});
/// njin::softbody3d_set_wind(ctx, flag, {6, 0, 1});
/// @endcode
/// @param ctx Context của engine.
/// @param desc Mô tả tấm vải.
/// @return Handle, hoặc không hợp lệ nếu `columns` hay `rows` ngoài khoảng.
softbody3d_handle cloth3d_create(context &ctx, const cloth3d_desc &desc);

/// Hủy vật mềm cùng model của nó (softbody3d_model()). Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
void softbody3d_destroy(context &ctx, softbody3d_handle handle);

/// Số đỉnh của vật mềm.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @return Số đỉnh, 0 nếu handle không hợp lệ.
i32 softbody3d_vertex_count(const context &ctx, softbody3d_handle handle);

/// Vị trí hiện tại của các đỉnh, trong thế giới.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @param out Mảng nhận vị trí, hoặc nullptr để chỉ đếm.
/// @param count Số phần tử của `out`.
/// @return Số đỉnh (softbody3d_vertex_count()); chỉ `count` đỉnh đầu được ghi.
i32 softbody3d_vertices(const context &ctx, softbody3d_handle handle, vec3 *out, i32 count);

/// Pháp tuyến hiện tại của các đỉnh (độ dài 1, trung bình các tam giác chung
/// đỉnh, hướng ra ngoài hay về phía trước của vải), để tự vẽ vật mềm.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @param out Mảng nhận pháp tuyến, hoặc nullptr để chỉ đếm.
/// @param count Số phần tử của `out`.
/// @return Số đỉnh; chỉ `count` đỉnh đầu được ghi.
i32 softbody3d_normals(const context &ctx, softbody3d_handle handle, vec3 *out, i32 count);

/// Các tam giác của mặt vật mềm, ba chỉ số đỉnh mỗi tam giác, ngược chiều kim
/// đồng hồ nhìn từ ngoài (từ phía trước của vải, phía +z lúc tạo).
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @param out Mảng nhận chỉ số, hoặc nullptr để chỉ đếm.
/// @param count Số phần tử của `out`.
/// @return Số chỉ số (gấp ba số tam giác); chỉ `count` số đầu được ghi.
i32 softbody3d_indices(const context &ctx, softbody3d_handle handle, u32 *out, i32 count);

/// Một model luôn có hình hiện tại của vật mềm, trong thế giới: engine cập nhật
/// nó sau mỗi bước vật lý. Vẽ bằng draw_model() với transform mặc định, đổi
/// màu và ảnh bằng model_material_set(). Model thuộc về vật mềm: không
/// model_unload() nó, softbody3d_destroy() làm việc đó. Vải có cả mặt sau.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @return Model, hoặc không hợp lệ nếu handle không hợp lệ. Vật nhiều đỉnh (quá
/// 65535, vải tính gấp đôi) được chia thành nhiều lưới trong cùng một model.
model_handle softbody3d_model(context &ctx, softbody3d_handle handle);

/// Đỉnh gần `point` nhất, để ghim hay kéo một chỗ của vật mềm.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @param point Điểm, thế giới.
/// @return Chỉ số đỉnh, -1 nếu handle không hợp lệ.
i32 softbody3d_nearest(const context &ctx, softbody3d_handle handle, vec3 point);

/// Ghim hay bỏ ghim một đỉnh. Đỉnh bị ghim đứng yên (trọng lực, va chạm, lò xo
/// không dời được nó) cho đến khi softbody3d_move_pinned() dời nó.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @param vertex Chỉ số đỉnh.
/// @param pinned `true` là ghim.
void softbody3d_pin(context &ctx, softbody3d_handle handle, i32 vertex, bool pinned);

/// Cho một đỉnh bị ghim tới `position` sau bước vật lý tới, kéo phần còn lại
/// theo: áo choàng ghim vào vai đang chạy, rèm kéo trên thanh treo. Gọi mỗi bước
/// cố định với vị trí mới; không gọi nữa thì đỉnh đứng lại ở đó.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @param vertex Chỉ số một đỉnh bị ghim; đỉnh không bị ghim thì bỏ qua.
/// @param position Vị trí muốn tới, thế giới.
void softbody3d_move_pinned(context &ctx, softbody3d_handle handle, i32 vertex, vec3 position);

/// Đặt gió thổi qua vật mềm, vận tốc của không khí (đơn vị mỗi giây). Mặt nào
/// chắn gió thì bị đẩy theo `drag` và diện tích của nó, nên vải bay phần phật
/// còn bóng chỉ trôi nhẹ. Mặc định không có gió (không khí đứng yên vẫn cản vật
/// mềm đang chuyển động).
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @param wind Vận tốc gió.
void softbody3d_set_wind(context &ctx, softbody3d_handle handle, vec3 wind);

/// Đẩy cả vật mềm một cú (xung lực, kg * đơn vị mỗi giây), chia đều cho các
/// đỉnh không bị ghim: cú đá vào quả bóng.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @param impulse Xung lực.
void softbody3d_add_impulse(context &ctx, softbody3d_handle handle, vec3 impulse);

/// Tâm của vật mềm (trung bình các đỉnh), để camera đi theo hay đặt âm thanh.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @return Tâm, thế giới; 0 nếu handle không hợp lệ.
vec3 softbody3d_position(const context &ctx, softbody3d_handle handle);

/// Số của game gắn vào vật mềm lúc tạo.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @return Số đó, hoặc 0 nếu handle không hợp lệ.
u64 softbody3d_user(const context &ctx, softbody3d_handle handle);

/// Vẽ vật mềm bằng gizmo (njin_gizmo.h) cho frame này: các cạnh của mặt, và đỉnh
/// bị ghim là chấm đỏ. Gọi mỗi frame, ở phase nào cũng được.
/// @param ctx Context của engine.
/// @param handle Vật mềm.
/// @param color Màu các cạnh.
void softbody3d_draw_debug(context &ctx, softbody3d_handle handle, rgba color = {0.3f, 0.9f, 1.0f, 1.0f});

/// Một bánh xe của njin::vehicle3d_desc.
struct vehicle3d_wheel {
  /// Tâm bánh khi giảm xóc duỗi hết (xe nhấc khỏi mặt đất), trong hệ trục của
  /// thân xe: x sang trái, y lên trên, z về phía trước.
  vec3 position{0.0f, 0.0f, 0.0f};
  f32 radius = 0.35f;     ///< Bán kính bánh.
  f32 width = 0.25f;      ///< Bề rộng bánh.
  bool steer = false;     ///< Bánh lái (bánh trước).
  bool drive = true;      ///< Động cơ kéo bánh này.
  bool handbrake = false; ///< Phanh tay khóa bánh này (bánh sau).
};

/// Mô tả một xe có bánh cho vehicle3d_create(). Thân xe là một body động hình
/// hộp `size` (hoặc bao lồi của `model`), đầu xe hướng +z: xe có `rotation` 0
/// chạy theo trục z, như nhân vật quay mặt theo `atan2(x, z)`.
struct vehicle3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f};    ///< Tâm thân xe.
  vec3 rotation{0.0f, 0.0f, 0.0f};    ///< Góc xoay, độ, cùng thứ tự với njin::transform3d.
  vec3 size{1.8f, 0.6f, 4.0f};        ///< Hộp của thân xe: rộng (x), cao (y), dài (z).
  model_handle model{};               ///< Lấy thân xe từ bao lồi của model này thay cho `size`.
  vec3 scale{1.0f, 1.0f, 1.0f};       ///< Tỉ lệ của `model`.
  f32 mass = 1200.0f;                 ///< Khối lượng, kg.
  /// Trọng tâm so với tâm thân xe. Thấp hơn tâm hộp thì xe khó lật khi cua gấp.
  vec3 center_of_mass{0.0f, -0.3f, 0.0f};
  /// Các bánh xe. nullptr là bốn bánh ở bốn góc dưới của `size`: hai bánh trước
  /// lái, cả bốn bánh kéo (dẫn động bốn bánh), hai bánh sau có phanh tay.
  const vehicle3d_wheel *wheels = nullptr;
  u32 wheel_count = 0;                ///< Số phần tử của `wheels`.
  f32 wheel_radius = 0.35f;           ///< Bán kính của bốn bánh mặc định.
  f32 wheel_width = 0.25f;            ///< Bề rộng của bốn bánh mặc định.
  f32 suspension = 0.3f;              ///< Hành trình giảm xóc, đơn vị thế giới.
  f32 suspension_frequency = 1.5f;    ///< Độ cứng của giảm xóc, dao động mỗi giây: 1 êm, 3 thể thao.
  f32 suspension_damping = 0.5f;      ///< Giảm chấn, 0 (nảy mãi) .. 1 (không nảy).
  f32 max_steer = 30.0f;              ///< Góc lái tối đa của bánh lái, độ.
  /// Mô-men xoắn lớn nhất của động cơ, N·m. Mạnh quá sức bám của lốp thì bánh
  /// quay trượt, và hộp số không lên số khi bánh đang trượt.
  f32 engine_torque = 300.0f;
  f32 max_rpm = 6000.0f;              ///< Vòng tua lớn nhất. Hộp số tự động.
  f32 brake_torque = 1500.0f;         ///< Lực phanh mỗi bánh, N·m.
  f32 handbrake_torque = 4000.0f;     ///< Lực phanh tay mỗi bánh, N·m.
  f32 friction = 0.5f;                ///< Ma sát của thân xe (khi lật, khi quệt tường), 0..1.
  u64 user = 0;                       ///< body3d_user() của thân xe.
};

/// Tạo một xe có bánh: thân xe là body động, mỗi bánh là một giảm xóc dò mặt đất
/// (không phải body), có động cơ, hộp số tự động, vi sai, phanh và phanh tay.
/// Lái bằng vehicle3d_set_input() mỗi bước cố định.
///
/// @code
/// car = njin::vehicle3d_create(ctx, {.position = {0, 1, 0}});
/// // Mỗi bước cố định:
/// njin::vehicle3d_set_input(ctx, car, njin::axis_value(ctx, gas), njin::axis_value(ctx, steer), 0.0f,
///                           njin::action_held(ctx, handbrake) ? 1.0f : 0.0f);
/// // Vẽ thân xe theo body3d_transform(ctx, njin::vehicle3d_body(ctx, car)), mỗi bánh theo
/// // vehicle3d_wheel_transform().
/// @endcode
/// @param ctx Context của engine.
/// @param desc Mô tả xe.
/// @return Handle, hoặc không hợp lệ nếu thân xe không dựng được hay không có bánh.
vehicle3d_handle vehicle3d_create(context &ctx, const vehicle3d_desc &desc);

/// Hủy xe cùng thân xe. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Xe.
void vehicle3d_destroy(context &ctx, vehicle3d_handle handle);

/// Đặt cách lái cho bước vật lý tới. Giữ nguyên cho tới lần gọi sau. Dùng cho mọi
/// loại xe: xe máy (motorcycle3d_create()) nghiêng vào cua theo `steer`; xe bánh
/// xích (tracked3d_create()) rẽ bằng hai dải xích, quay tại chỗ khi gần đứng yên mà
/// chỉ lái không ga, và `handbrake` là phanh.
/// @param ctx Context của engine.
/// @param handle Xe.
/// @param throttle Ga, -1 (lùi) .. 1 (tiến). Đang chạy tới mà ga âm thì xe phanh trước, dừng hẳn rồi mới lùi.
/// @param steer Lái, -1 (trái) .. 1 (phải).
/// @param brake Phanh, 0..1.
/// @param handbrake Phanh tay, 0..1.
void vehicle3d_set_input(context &ctx, vehicle3d_handle handle, f32 throttle, f32 steer, f32 brake, f32 handbrake);

/// Thân xe, để đọc vị trí (body3d_transform()), vận tốc, hay đẩy nó. Không hủy
/// nó bằng body3d_destroy(): hủy cả xe.
/// @param ctx Context của engine.
/// @param handle Xe.
/// @return Body, hoặc không hợp lệ nếu handle không hợp lệ.
body3d_handle vehicle3d_body(const context &ctx, vehicle3d_handle handle);

/// Số bánh của xe.
/// @param ctx Context của engine.
/// @param handle Xe.
/// @return Số bánh, 0 nếu handle không hợp lệ.
i32 vehicle3d_wheel_count(const context &ctx, vehicle3d_handle handle);

/// Chỗ và góc của một bánh lúc này (đã tính giảm xóc, góc lái và bánh quay),
/// trong thế giới. Trục y của transform là trục bánh, nên vẽ được bằng một hình
/// trụ (njin::shape3d_cylinder, `radius` là bán kính bánh, `height` là bề rộng).
/// @param ctx Context của engine.
/// @param handle Xe.
/// @param wheel Chỉ số bánh.
/// @return Vị trí và góc xoay, độ; mặc định nếu handle hay `wheel` không hợp lệ.
transform3d vehicle3d_wheel_transform(const context &ctx, vehicle3d_handle handle, i32 wheel);

/// Bánh có đang chạm đất không.
/// @param ctx Context của engine.
/// @param handle Xe.
/// @param wheel Chỉ số bánh.
/// @return `true` nếu chạm.
bool vehicle3d_wheel_grounded(const context &ctx, vehicle3d_handle handle, i32 wheel);

/// Vòng tua động cơ lúc này, cho tiếng máy và đồng hồ.
/// @param ctx Context của engine.
/// @param handle Xe.
/// @return Vòng mỗi phút, 0 nếu handle không hợp lệ.
f32 vehicle3d_rpm(const context &ctx, vehicle3d_handle handle);

/// Số đang vào: -1 số lùi, 0 số không, 1 trở lên là số tiến.
/// @param ctx Context của engine.
/// @param handle Xe.
/// @return Số, 0 nếu handle không hợp lệ.
i32 vehicle3d_gear(const context &ctx, vehicle3d_handle handle);

/// Mô tả một xe máy cho motorcycle3d_create(): thân là một body động hình hộp
/// `size` (hoặc bao lồi của `model`), một bánh trước lái trên phuộc nghiêng, một
/// bánh sau kéo. Đầu xe hướng +z như njin::vehicle3d_desc. Số mặc định là một xe
/// máy 240 kg (theo mẫu xe máy của Jolt), chạy ổn không cần chỉnh.
struct motorcycle3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f};    ///< Tâm thân xe.
  vec3 rotation{0.0f, 0.0f, 0.0f};    ///< Góc xoay, độ, cùng thứ tự với njin::transform3d.
  vec3 size{0.4f, 0.6f, 0.8f};        ///< Hộp của thân xe: rộng (x), cao (y), dài (z).
  model_handle model{};               ///< Lấy thân xe từ bao lồi của model này thay cho `size`.
  vec3 scale{1.0f, 1.0f, 1.0f};       ///< Tỉ lệ của `model`.
  f32 mass = 240.0f;                  ///< Khối lượng cả xe và người lái, kg.
  vec3 center_of_mass{0.0f, -0.3f, 0.0f}; ///< Trọng tâm so với tâm thân xe.
  f32 wheelbase = 1.5f;               ///< Khoảng cách giữa hai bánh, đơn vị thế giới.
  f32 wheel_radius = 0.31f;           ///< Bán kính bánh.
  f32 wheel_width = 0.05f;            ///< Bề rộng bánh.
  f32 suspension = 0.2f;              ///< Hành trình giảm xóc.
  f32 caster = 30.0f;                 ///< Góc nghiêng của phuộc trước, độ.
  f32 max_steer = 30.0f;              ///< Góc lái tối đa, độ. Engine tự giảm khi xe chạy nhanh để xe không đổ.
  f32 max_lean = 45.0f;               ///< Góc nghiêng tối đa khi vào cua, độ.
  f32 engine_torque = 150.0f;         ///< Mô-men xoắn lớn nhất của động cơ, N·m.
  f32 max_rpm = 10000.0f;             ///< Vòng tua lớn nhất. Hộp số tự động, sáu số.
  f32 brake_torque = 500.0f;          ///< Lực phanh bánh trước, N·m; bánh sau một nửa.
  f32 friction = 0.5f;                ///< Ma sát của thân xe (khi đổ, khi quệt tường), 0..1.
  u64 user = 0;                       ///< body3d_user() của thân xe.
};

/// Tạo một xe máy (bộ điều khiển xe máy của Jolt): một lò xo giữ xe đứng thẳng
/// và nghiêng vào cua theo tốc độ và góc lái, nên xe không đổ khi đứng yên hay
/// chạy chậm. Là một njin::vehicle3d_handle: lái bằng vehicle3d_set_input() (phanh
/// tay khóa bánh sau), vẽ bằng vehicle3d_body() và vehicle3d_wheel_transform(),
/// mọi hàm `vehicle3d_*` dùng được.
///
/// @code
/// bike = njin::motorcycle3d_create(ctx, {.position = {0, 1, 0}});
/// // Mỗi bước cố định: ga, lái, phanh.
/// njin::vehicle3d_set_input(ctx, bike, gas, steer, brake, 0.0f);
/// @endcode
/// @param ctx Context của engine.
/// @param desc Mô tả xe.
/// @return Handle, hoặc không hợp lệ nếu thân xe không dựng được.
vehicle3d_handle motorcycle3d_create(context &ctx, const motorcycle3d_desc &desc);

/// Mô tả một xe bánh xích (xe tăng, máy xúc) cho tracked3d_create(): thân là một
/// body động hình hộp `size` (hoặc bao lồi của `model`), mỗi bên một dải xích chạy
/// trên `wheels_per_side` bánh, bánh cuối mỗi bên là bánh kéo. Số mặc định là một
/// xe tăng 4 tấn (theo mẫu xe tăng của Jolt).
struct tracked3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f};    ///< Tâm thân xe.
  vec3 rotation{0.0f, 0.0f, 0.0f};    ///< Góc xoay, độ, cùng thứ tự với njin::transform3d.
  vec3 size{3.4f, 1.0f, 6.4f};        ///< Hộp của thân xe: rộng (x), cao (y), dài (z).
  model_handle model{};               ///< Lấy thân xe từ bao lồi của model này thay cho `size`.
  vec3 scale{1.0f, 1.0f, 1.0f};       ///< Tỉ lệ của `model`.
  f32 mass = 4000.0f;                 ///< Khối lượng, kg.
  vec3 center_of_mass{0.0f, -0.5f, 0.0f}; ///< Trọng tâm so với tâm thân xe.
  i32 wheels_per_side = 9;            ///< Số bánh mỗi bên, 3..16: hai bánh đầu cuối nâng cao, các bánh giữa đỡ xe.
  f32 wheel_radius = 0.3f;            ///< Bán kính bánh.
  f32 wheel_width = 0.1f;             ///< Bề rộng bánh (dải xích).
  f32 suspension = 0.2f;              ///< Hành trình giảm xóc của các bánh giữa.
  f32 engine_torque = 500.0f;         ///< Mô-men xoắn lớn nhất của động cơ, N·m.
  f32 max_rpm = 6000.0f;              ///< Vòng tua lớn nhất. Hộp số tự động.
  f32 brake_torque = 15000.0f;        ///< Lực phanh mỗi dải xích, N·m.
  f32 friction = 0.5f;                ///< Ma sát của thân xe, 0..1.
  u64 user = 0;                       ///< body3d_user() của thân xe.
};

/// Tạo một xe bánh xích (bộ điều khiển xe xích của Jolt): rẽ bằng cách cho hai
/// dải xích chạy khác tốc độ, quay tại chỗ khi chúng chạy ngược chiều nhau. Là
/// một njin::vehicle3d_handle: vehicle3d_set_input() lái như ô tô (đứng yên mà chỉ
/// lái thì xe quay tại chỗ), vehicle3d_set_tracks() điều khiển từng dải xích,
/// vehicle3d_track_speed() cho tốc độ xích để cuộn ảnh của nó.
/// @param ctx Context của engine.
/// @param desc Mô tả xe.
/// @return Handle, hoặc không hợp lệ nếu thân xe không dựng được.
vehicle3d_handle tracked3d_create(context &ctx, const tracked3d_desc &desc);

/// Điều khiển thẳng hai dải xích của xe bánh xích cho bước vật lý tới, giữ nguyên
/// tới lần gọi sau: mỗi bên -1 (lùi hết cỡ) .. 1 (tiến hết cỡ). `left = 1, right =
/// -1` là quay tại chỗ sang phải. Xe không phải bánh xích thì bỏ qua.
/// @param ctx Context của engine.
/// @param handle Xe bánh xích (tracked3d_create()).
/// @param left Dải xích trái (phía +x).
/// @param right Dải xích phải (phía -x).
/// @param brake Phanh, 0..1.
void vehicle3d_set_tracks(context &ctx, vehicle3d_handle handle, f32 left, f32 right, f32 brake);

/// Tốc độ của một dải xích, để cuộn ảnh xích theo đúng tốc độ xe chạy.
/// @param ctx Context của engine.
/// @param handle Xe bánh xích.
/// @param side 0 là dải trái (+x), 1 là dải phải (-x).
/// @return Đơn vị thế giới mỗi giây, âm khi xích chạy lùi; 0 nếu không phải xe bánh xích.
f32 vehicle3d_track_speed(const context &ctx, vehicle3d_handle handle, i32 side);

/// Vẽ xe bằng gizmo (njin_gizmo.h) cho frame này: hộp thân xe, mỗi bánh là một
/// vòng tròn với trục, giảm xóc từ chỗ gắn tới tâm bánh, và điểm bánh chạm đất
/// (vàng). Gọi mỗi frame, ở phase nào cũng được.
/// @param ctx Context của engine.
/// @param handle Xe.
/// @param color Màu thân và bánh.
void vehicle3d_draw_debug(context &ctx, vehicle3d_handle handle, rgba color = {1.0f, 0.6f, 0.2f, 1.0f});

/// Component: entity đi theo một body vật lý. Engine đọc và ghi
/// njin::transform3d của entity quanh mỗi bước mô phỏng:
/// - body động: sau bước, vị trí và góc xoay của body ghi vào transform;
/// - body kinematic: trước bước, body được đưa tới transform (như
///   body3d_move_kinematic()), nên game chỉ cần dời transform;
/// - body tĩnh: không làm gì.
///
/// Gỡ component hay hủy entity thì body bị hủy theo.
///
/// @code
/// const auto crate = reg.create();
/// reg.emplace<njin::transform3d>(crate);
/// reg.emplace<njin::shape3d_render>(crate, njin::shape3d_render{.shape = {.kind = njin::shape3d_box}});
/// reg.emplace<njin::body3d>(crate, njin::body3d_create(ctx, {.position = {0, 3, 0},
///                                                            .motion = njin::body3d_dynamic}));
/// @endcode
struct body3d {
  body3d_handle handle{}; ///< Body từ body3d_create().
};

/// Component: entity đi theo một nhân vật vật lý. Sau mỗi bước mô phỏng, vị trí
/// chân của nhân vật ghi vào `transform3d::position` của entity (góc xoay do game
/// đặt). Gỡ component hay hủy entity thì nhân vật bị hủy theo.
struct character3d {
  character3d_handle handle{}; ///< Nhân vật từ character3d_create().
};

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
