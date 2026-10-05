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

/// Bắn một tia vào các body (không trúng nhân vật, đi xuyên sensor): đạn, tầm nhìn,
/// chọn vật bằng chuột.
/// @param ctx Context của engine.
/// @param ray Tia (hướng độ dài 1).
/// @param max_distance Xa nhất còn tính, đơn vị thế giới.
/// @param body Nếu khác nullptr, nhận body bị trúng (không hợp lệ nếu trượt).
/// @return Điểm chạm gần nhất, nếu có.
ray3d_hit physics3d_raycast(const context &ctx, const ray3d &ray, f32 max_distance,
                            body3d_handle *body = nullptr);

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
