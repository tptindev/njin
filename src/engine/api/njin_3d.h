#pragma once
// Thuộc feature 3d: game cần bật nó bằng njin_use(<game> FEATURES 3d)
// (cmake/njin.cmake), hoặc link njin::rt.
#if !defined(NJIN_ENGINE_INTERNAL) && !defined(NJIN_FEATURE_3D)
#error "njin_3d.h is part of the njin feature '3d', which this game does not use: add it with njin_use(<game> FEATURES 3d)"
#endif
#include "_math.h"
#include "njin_particles.h"

namespace njin {
struct context;

/// @addtogroup grp_3d
/// @{

/// Phép chiếu của camera3d.
enum camera3d_projection {
  camera3d_perspective,  ///< Phối cảnh: vật xa nhỏ đi, theo `fovy` (mặc định).
  /// Song song (orthographic): vật giữ cỡ dù xa hay gần, theo `ortho_height`.
  /// `position` vẫn là chỗ đặt mắt (cho bóng đổ, sương, âm thanh): đặt nó lùi về
  /// sau, đủ xa để thấy cả cảnh trong `near_plane`..`far_plane`.
  camera3d_orthographic,
};

/// Camera cho begin_3d(): phối cảnh (mặc định) hay song song, xem `projection`.
///
/// Trục y hướng lên, hệ tay phải (giống glTF và raylib): nhìn theo `-z` thì `+x`
/// ở bên phải.
struct camera3d {
  vec3 position{0.0f, 0.0f, 0.0f};  ///< Vị trí mắt.
  vec3 target{0.0f, 0.0f, -1.0f};   ///< Điểm camera nhìn vào.
  vec3 up{0.0f, 1.0f, 0.0f};        ///< Hướng lên của camera.
  f32 fovy = 60.0f;                 ///< Góc nhìn dọc, tính bằng độ.
  f32 near_plane = 0.05f;           ///< Khoảng cách gần nhất còn được vẽ.
  f32 far_plane = 1000.0f;          ///< Khoảng cách xa nhất còn được vẽ.
  /// Vẽ cả các entity 3D (njin::model3d, njin::shape3d_render, đèn njin::light3d_source)
  /// trong lần vẽ này. Tắt cho một cảnh phụ (ví dụ model xoay trong menu) chỉ vẽ
  /// những gì game gọi.
  bool entities = true;
  /// Phép chiếu. camera3d_orthographic là nhìn song song: vật xa không nhỏ đi,
  /// cho game chiến thuật, xây thành phố, puzzle nhìn nghiêng và góc isometric
  /// dựng bằng 3D. Khi đó `fovy` bị bỏ qua và `ortho_height` là chiều cao vùng nhìn.
  camera3d_projection projection = camera3d_perspective;
  /// Chiều cao vùng nhìn của camera3d_orthographic, đơn vị thế giới (chiều rộng
  /// theo tỉ lệ khung hình). Nhỏ đi là phóng to.
  f32 ortho_height = 10.0f;
};

/// Bắt đầu vẽ 3D qua `camera`. Kết thúc bằng end_3d().
///
/// Chỉ gọi trong `phase_render`. Giữa hai lệnh, các hàm `draw_*3d` và
/// draw_model() vẽ có depth test, trên nền những gì đã vẽ trước đó trong frame.
/// Tỉ lệ khung hình lấy theo screen_size(), nên virtual size vẫn đúng. Gọi lồng
/// nhau hoặc gọi ngoài `phase_render` bị bỏ qua, kèm cảnh báo.
///
/// camera_shake() cũng rung camera này: `shake_config::max_offset` (pixel màn hình)
/// thành góc lệch hướng nhìn, `shake_config::max_angle` thành góc nghiêng. Các hiệu
/// ứng chung khác áp dụng cho 3D như cho 2D, không cần gì thêm: hitstop(),
/// screen_flash(), post_fx_set() và camera_set_post_shader().
/// @param ctx Context của engine.
/// @param camera Camera dùng cho lần vẽ này.
void begin_3d(context &ctx, const camera3d &camera);

/// Như begin_3d() nhưng vẽ lần 3D này vào render texture `target` thay vì lên
/// màn hình: một con mắt thứ hai trong cùng frame (camera của một nhân vật, gương
/// chiếu hậu). Xóa `target` bằng màu `clear` trước, tỉ lệ khung hình lấy theo kích
/// thước của nó. Cũng chỉ gọi trong `phase_render` và kết thúc bằng end_3d();
/// nhiều lần như vậy trong một frame được, mỗi lần một lượt vẽ đầy đủ (kể cả bóng đổ
/// nếu light3d_set() bật), nên tốn như vẽ cảnh thêm một lần.
///
/// camera_shake() không rung camera này, gizmo không vẽ vào đây, và độ sâu của nó
/// không dùng cho `post_fx::dof`. Đèn (light3d_add()) phải thêm lại cho lần vẽ này.
/// Ảnh trong render texture lưu ngược trục dọc như mọi render texture.
/// @param ctx Context của engine.
/// @param camera Camera dùng cho lần vẽ này.
/// @param target Render texture nhận ảnh. Handle không hợp lệ thì bỏ qua, kèm cảnh báo.
/// @param clear Màu xóa `target` trước khi vẽ.
void begin_3d(context &ctx, const camera3d &camera, render_texture_handle target, rgba clear);

/// Kết thúc vẽ 3D và quay về vẽ 2D trong không gian thế giới.
///
/// Không có begin_3d() tương ứng thì không làm gì. Nếu game quên gọi, engine tự
/// đóng ở cuối `phase_render` và cảnh báo.
/// @param ctx Context của engine.
void end_3d(context &ctx);

/// Cách nén ánh sáng mạnh vào khoảng màn hình hiển thị được (light3d::tonemap).
enum tonemap3d {
  /// Tắt (mặc định): màu tính ra được ghi thẳng, quá 1 thì cháy trắng, như trước
  /// giờ. Màu, ánh sáng và ảnh dùng như nhập vào, không đổi không gian màu.
  tonemap3d_none,
  /// Chỉ nhân `exposure` rồi cắt ở 1, nhưng tính trong không gian tuyến tính.
  tonemap3d_linear,
  /// ACES (như three.js ACESFilmic): tương phản cao, chỗ rất sáng ngả vàng nhẹ.
  tonemap3d_aces,
  /// AgX (mặc định của Blender): mềm, giữ màu khi rất sáng, chỗ cháy về trắng.
  tonemap3d_agx,
  /// Khronos PBR Neutral: giữ màu vật liệu gần như nguyên, chỉ nén chỗ sáng.
  /// Hợp với sản phẩm, đồ vật, khi màu phải đúng như ảnh.
  tonemap3d_neutral,
};

/// Ánh sáng chung của cảnh 3D: mặt trời (ánh sáng hướng), ánh sáng nền, bóng đổ
/// của mặt trời và sương mù. Đèn điểm và đèn nón thêm bằng light3d_add().
///
/// Mỗi mặt sáng theo góc giữa pháp tuyến của nó và hướng tới đèn (Lambert), cộng
/// điểm sáng bóng Blinn-Phong theo njin::material3d, cộng `ambient` để mặt khuất
/// không đen hẳn. Vật liệu `pbr` thay Blinn-Phong bằng metallic/roughness, và
/// `environment` cho chúng ánh sáng và phản chiếu từ một ảnh môi trường.
struct light3d {
  vec3 direction{-0.4f, -1.0f, -0.3f};  ///< Hướng nắng chiếu tới (không cần độ dài 1).
  rgba color{1.0f, 1.0f, 1.0f, 1.0f};   ///< Màu và cường độ của nắng. Đen là tắt mặt trời.
  rgba ambient{0.35f, 0.35f, 0.4f, 1.0f}; ///< Ánh sáng nền, chiếu đều mọi mặt.
  /// Mặt trời đổ bóng. Mọi hình đục (màu có `a` bằng 1) và model đổ bóng lên nhau,
  /// trừ khi njin::material3d tắt `cast_shadows`.
  bool shadows = false;
  /// Bóng được tính trong một hình hộp cạnh `2 * shadow_range` quanh chỗ camera
  /// nhìn. Nhỏ thì bóng sắc hơn nhưng vật ở xa không có bóng.
  f32 shadow_range = 30.0f;
  i32 shadow_size = 2048;       ///< Cạnh của ảnh bóng, pixel. Lớn thì sắc hơn, tốn bộ nhớ hơn.
  f32 shadow_softness = 1.0f;   ///< Độ mềm mép bóng, tính bằng pixel của ảnh bóng. 0 là sắc.
  /// Cạnh ảnh bóng của đèn điểm và đèn nón có `light3d_source::shadows`, pixel, cho
  /// mỗi mặt (đèn điểm có 6 mặt, đèn nón 1).
  i32 source_shadow_size = 512;
  rgba fog_color{0.6f, 0.65f, 0.75f, 1.0f}; ///< Màu sương mù, thường bằng màu nền.
  /// Mật độ sương theo khoảng cách tới camera: độ trong còn `exp(-(density * d)^2)`.
  /// 0 là không sương.
  f32 fog_density = 0.0f;

  /// @name Tone mapping và môi trường (PBR)
  /// @{

  /// Nén ánh sáng mạnh cho vừa màn hình. Khác tonemap3d_none thì njin tính ánh
  /// sáng trong không gian tuyến tính (màu, đèn và ảnh coi như sRGB rồi đổi ra),
  /// nén theo cách chọn rồi đổi lại sRGB, ngay trong shader 3D: đèn mạnh hơn 1
  /// không còn cháy trắng, vật liệu `pbr` ra đúng màu. Áp cho mọi hình được chiếu
  /// sáng (hình khối, model, hình SDF, instanced, địa hình); vật `unlit`, hạt, nền
  /// và màu sương giữ nguyên như nhập. Bloom (post_fx_set()) vẫn đọc ảnh đã nén.
  tonemap3d tonemap = tonemap3d_none;
  /// Độ phơi sáng, nhân vào ánh sáng trước khi nén: 2 là sáng gấp đôi. Chỉ khi
  /// `tonemap` khác tonemap3d_none.
  f32 exposure = 1.0f;
  /// Ảnh môi trường bao quanh cảnh (ảnh toàn cảnh equirectangular 2:1, `.hdr`
  /// hay png/jpg) chiếu sáng và phản chiếu lên vật liệu `pbr`: thay cho `ambient`
  /// với chúng, kim loại phản chiếu nó. Lần đầu gặp một ảnh, njin dựng sẵn ánh
  /// sáng khuếch tán (spherical harmonics) và các mức phản chiếu theo độ nhám, tốn
  /// vài mili giây; đổi ảnh thì dựng lại. Không hợp lệ là không có (dùng `ambient`).
  /// Nên dùng cùng `tonemap`: ảnh `.hdr` sáng hơn 1 nhiều.
  texture_handle environment{};
  f32 environment_intensity = 1.0f; ///< Độ sáng của ảnh môi trường.
  f32 environment_rotation = 0.0f;  ///< Xoay ảnh môi trường quanh trục y, độ.
  /// Vẽ ảnh môi trường làm nền trời, sau mọi thứ của lần vẽ (cùng tone mapping).
  bool environment_visible = false;
  /// @}

  /// @name HDR và tự chỉnh độ phơi sáng
  /// @{

  /// Vẽ cảnh 3D vào một ảnh HDR (số thực 16 bit, sáng hơn 1 vẫn giữ nguyên) rồi tone
  /// map một lần ở cuối, như Bevy và Unreal, thay cho tone map trong từng shader. Hiệu
  /// ứng màn hình 3D (SSAO, SSR, TAA, tia nắng, mờ chuyển động) chạy trên ánh sáng
  /// thật, bloom của `post3d::bloom` lan theo năng lượng, và tự chỉnh độ phơi sáng
  /// đo được ảnh. Chỉ khi `tonemap` khác tonemap3d_none, cho lần vẽ vào thế giới
  /// (lần vẽ vào render texture vẫn tone map trong từng shader).
  ///
  /// Mọi thứ của lần vẽ đi qua tone mapping, như Bevy: vật được chiếu sáng, nước,
  /// bầu trời (draw_sky3d(), `environment_visible`) nhân thêm phơi sáng; vật `unlit`,
  /// hạt 3D, màu sương và nền (màu xóa, 2D vẽ trước begin_3d()) giữ màu như nhập,
  /// chỉ qua đường cong. Với tonemap3d_linear và `exposure` 1 thì chúng giữ nguyên
  /// màu, với ACES hay AgX thì theo đường cong đó. Shader riêng của game nhận `int`
  /// `hdrOut` (1 lúc này): khi đó hãy ghi ánh sáng tuyến tính đã nhân `exposure`,
  /// không tone map.
  bool hdr = false;
  /// Tự chỉnh độ phơi sáng theo độ sáng trung bình của ảnh (đo ưu tiên giữa màn
  /// hình), như mắt quen dần với chỗ tối hay sáng: vào hang thì sáng lên dần, ra nắng
  /// thì tối đi. Nhân thêm vào `exposure`. Chỉ khi `hdr`.
  bool auto_exposure = false;
  f32 auto_exposure_speed = 1.5f; ///< Tốc độ quen dần, mỗi giây. Lớn là đổi nhanh.
  f32 auto_exposure_min = 0.25f;  ///< Hệ số phơi sáng nhỏ nhất mà nó chọn.
  f32 auto_exposure_max = 4.0f;   ///< Hệ số phơi sáng lớn nhất mà nó chọn.
  /// Độ sáng mà ảnh trung bình được đưa về (xám giữa, 0.18 như máy ảnh). Lớn thì cả
  /// cảnh sáng hơn.
  f32 auto_exposure_key = 0.18f;
  /// @}

  /// @name Bóng đổ nhiều tầng (cascaded shadow maps)
  /// @{

  /// Số tầng bóng của mặt trời, 1..4. 1 (mặc định) là một ảnh bóng như trước, phủ
  /// hình hộp `shadow_range` quanh chỗ camera nhìn. Từ 2 trở lên, khoảng từ camera
  /// tới `shadow_range` mét được chia thành từng đoạn, mỗi đoạn một ảnh bóng cạnh
  /// `shadow_size`: gần camera bóng sắc, xa vẫn có bóng. Mỗi tầng vẽ thêm mọi hình
  /// đổ bóng một lần.
  i32 shadow_cascades = 1;
  /// Cách chia các tầng, 0..1: 0 chia đều, 1 chia theo cấp số nhân (tầng gần rất
  /// ngắn). Khoảng 0.75 hợp với đa số cảnh.
  f32 shadow_cascade_split = 0.75f;
  /// @}

  /// @name Nhiều đèn
  /// @{

  /// Số đèn tối đa của một lần vẽ, tới njin::light3d_limit. Mặc định njin::light3d_max
  /// (16): đèn quá số đó bị bỏ qua, như trước. Lớn hơn thì njin chia khung nhìn thành
  /// các cụm (clustered lighting, như Bevy) và mỗi điểm ảnh chỉ tính các đèn chạm cụm
  /// của nó, nên hàng trăm đèn nhỏ vẫn nhanh.
  i32 lights_max = 16;
  /// Số đèn điểm và đèn nón có `shadows` được đổ bóng, tới njin::light3d_shadow_limit.
  /// Mặc định njin::light3d_shadow_max (4). Mỗi đèn đổ bóng thêm một hàng vào ảnh bóng
  /// (6 ô `source_shadow_size` cho đèn điểm) và vẽ lại mọi hình đổ bóng 6 lần (đèn
  /// điểm) hoặc 1 lần (đèn nón).
  i32 source_shadows = 4;
  /// @}

  /// @name Sương khối (volumetric fog)
  /// Sương có thật trong không gian: ánh nắng và ánh đèn tán xạ trong nó thành những
  /// luồng sáng, bóng của vật cắt thành vệt tối (nắng lọt qua cửa sổ, đèn pin trong
  /// sương, đèn đường đêm mưa). Tính bằng cách dò tia từ camera qua từng điểm ảnh, ở
  /// nửa độ phân giải. Đẹp nhất với `hdr`. Chỉ cho lần vẽ vào thế giới.
  /// @{

  /// Mật độ sương, mỗi mét. 0 (mặc định) là tắt; 0.02..0.1 là sương nhẹ đến dày.
  f32 volume_density = 0.0f;
  /// Màu của sương (phần ánh sáng nó tán xạ lại), thường là trắng hay xám xanh.
  rgba volume_color{1.0f, 1.0f, 1.0f, 1.0f};
  /// Sương dày ở dưới `volume_base` (độ cao y) và mỏng dần lên trên, theo
  /// `exp(-volume_falloff * (y - volume_base))`. `volume_falloff` 0 là đều mọi độ cao.
  f32 volume_base = 0.0f;
  f32 volume_falloff = 0.0f; ///< Độ mỏng dần theo độ cao, mỗi mét. 0 là đều.
  /// Sương loang lổ thành từng đám, 0 (đều) đến 1 (thành đám rõ rệt).
  f32 volume_noise = 0.0f;
  f32 volume_noise_size = 6.0f;             ///< Cỡ một đám sương, mét.
  vec3 volume_wind{0.6f, 0.0f, 0.2f};       ///< Đám sương trôi theo gió, mét mỗi giây.
  /// Hướng tán xạ, -1..1 (Henyey-Greenstein): dương thì sáng nhất khi nhìn về phía
  /// nguồn sáng (quầng quanh mặt trời, đèn pha), 0 thì đều mọi hướng.
  f32 volume_anisotropy = 0.5f;
  f32 volume_distance = 60.0f; ///< Dò tới bấy nhiêu mét từ camera; xa hơn coi như hết sương.
  i32 volume_steps = 32;       ///< Số bước dò mỗi tia, 8..128. Nhiều thì mịn hơn, tốn hơn.
  f32 volume_sun = 1.0f;       ///< Độ sáng của nắng trong sương, nhân với màu nắng.
  f32 volume_ambient = 0.3f;   ///< Phần ánh sáng nền (`ambient`) mà sương tán xạ.
  bool volume_lamps = true;    ///< Đèn điểm và đèn nón cũng tán xạ trong sương (theo bóng của chúng).
  /// @}
};

/// Đặt ánh sáng chung. Có hiệu lực từ begin_3d() tiếp theo.
/// @param ctx Context của engine.
/// @param light Ánh sáng mới.
void light3d_set(context &ctx, const light3d &light);

/// Ánh sáng chung đang dùng.
/// @param ctx Context của engine.
/// @return Giá trị đặt bởi light3d_set(), hoặc mặc định của njin::light3d.
light3d light3d_get(const context &ctx);

/// Loại đèn của njin::light3d_source.
enum light3d_kind {
  light3d_point, ///< Đèn điểm: tỏa đều mọi hướng (bóng đèn, đuốc).
  light3d_spot,  ///< Đèn nón: chiếu theo `direction` trong một nón (đèn pin, đèn sân khấu).
};

/// Một đèn điểm hoặc đèn nón. Sáng giảm dần tới 0 ở `radius`.
///
/// Thêm cho một lần vẽ bằng light3d_add(), hoặc làm component: entity có
/// njin::light3d_source và njin::transform3d chiếu sáng mọi lần vẽ 3D (có
/// `camera3d::entities`), đặt tại `transform3d::position` (`position` bị bỏ qua).
struct light3d_source {
  light3d_kind kind = light3d_point; ///< Loại đèn.
  vec3 position{0.0f, 0.0f, 0.0f};   ///< Vị trí.
  vec3 direction{0.0f, -1.0f, 0.0f}; ///< Hướng chiếu, chỉ cho đèn nón.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu.
  f32 intensity = 1.0f;              ///< Độ sáng, nhân vào màu. Có thể lớn hơn 1.
  f32 radius = 10.0f;                ///< Tầm với, đơn vị thế giới.
  f32 cone = 60.0f;                  ///< Góc mở của nón, độ (cả hai bên), chỉ cho đèn nón.
  f32 softness = 0.25f;              ///< Phần mép nón mờ dần, 0..1, chỉ cho đèn nón.
  /// Đổ bóng, như `light3d::shadows` của mặt trời, trong tầm `radius`. Mỗi lần vẽ có
  /// tối đa njin::light3d_shadow_max đèn đổ bóng (đèn sau đó chiếu sáng mà không đổ
  /// bóng). Tốn: mọi hình đổ bóng được vẽ thêm 6 lần cho một đèn điểm, 1 lần cho một
  /// đèn nón, nên chỉ bật cho vài đèn quan trọng (đèn pin, đèn treo giữa phòng).
  bool shadows = false;
};

/// Số đèn tối đa trong một lần vẽ 3D, mặc định. Đèn thêm quá số này bị bỏ qua,
/// trừ khi light3d::lights_max lớn hơn.
inline constexpr i32 light3d_max = 16;

/// Số đèn điểm và đèn nón tối đa được đổ bóng trong một lần vẽ 3D, mặc định
/// (light3d::source_shadows).
inline constexpr i32 light3d_shadow_max = 4;

/// Giới hạn trên của light3d::lights_max.
inline constexpr i32 light3d_limit = 1024;

/// Giới hạn trên của light3d::source_shadows.
inline constexpr i32 light3d_shadow_limit = 16;

/// Thêm một đèn cho lần vẽ 3D đang mở. Gọi mỗi frame, giữa begin_3d() và end_3d()
/// (thứ tự với các lệnh vẽ không quan trọng: mọi đèn chiếu lên mọi hình của lần vẽ).
/// @param ctx Context của engine.
/// @param light Đèn.
void light3d_add(context &ctx, const light3d_source &light);

/// Một probe phản chiếu: njin chụp cảnh quanh `position` (6 hướng) thành một ảnh
/// môi trường riêng, rồi vật liệu `pbr` trong hộp `size` quanh nó lấy ánh sáng và
/// phản chiếu từ ảnh đó thay cho light3d::environment: trong phòng thì phản chiếu
/// tường và đồ trong phòng, không phải bầu trời ngoài kia. Như reflection probe
/// của Bevy, Unity và Unreal.
///
/// Probe được chụp ở lần vẽ 3D vào thế giới kế tiếp, từ chính các lệnh vẽ của lần
/// vẽ đó (hình đục, model, địa hình, bầu trời; không có hạt, nước, kính), rồi giữ
/// nguyên cho tới probe3d_refresh(), trừ khi `live`.
struct probe3d_desc {
  vec3 position{0.0f, 1.5f, 0.0f}; ///< Chỗ chụp, thường giữa phòng ở tầm mắt; cũng là tâm hộp.
  /// Cỡ hộp ảnh hưởng theo x, y, z, thường bằng cỡ căn phòng. Vật liệu trong hộp
  /// dùng probe này.
  vec3 size{8.0f, 3.0f, 8.0f};
  /// Bề dày của dải mép hộp mà probe mờ dần sang probe bên cạnh hay môi trường, mét.
  f32 blend = 1.0f;
  f32 intensity = 1.0f; ///< Độ sáng của ánh sáng và phản chiếu lấy từ probe.
  /// Sửa thị sai theo hộp (box projection): phản chiếu trên sàn và tường khớp với vị
  /// trí thật của vật trong phòng, thay vì như ở xa vô tận. Nên bật cho phòng kín.
  bool parallax = true;
  i32 resolution = 128; ///< Cạnh mỗi mặt khi chụp, pixel (32..512).
  /// Chụp lại mỗi frame (cảnh quanh probe đổi liên tục). Tốn: vẽ lại cảnh 6 lần.
  bool live = false;
};

/// Số probe phản chiếu tối đa cùng lúc.
inline constexpr i32 probe3d_max = 8;

/// Thêm một probe phản chiếu. Quá njin::probe3d_max thì trả về handle không hợp lệ.
/// @param ctx Context của engine.
/// @param desc Probe.
/// @return Handle của probe.
probe3d_handle probe3d_add(context &ctx, const probe3d_desc &desc);

/// Đổi cách đặt của một probe; nó được chụp lại ở lần vẽ kế tiếp.
/// @param ctx Context của engine.
/// @param handle Probe. Handle không hợp lệ bị bỏ qua.
/// @param desc Cách đặt mới.
void probe3d_set(context &ctx, probe3d_handle handle, const probe3d_desc &desc);

/// Chụp lại probe ở lần vẽ 3D kế tiếp (cảnh quanh nó đã đổi: đèn tắt, đồ dời chỗ).
/// @param ctx Context của engine.
/// @param handle Probe. Handle không hợp lệ bị bỏ qua.
void probe3d_refresh(context &ctx, probe3d_handle handle);

/// Bỏ một probe.
/// @param ctx Context của engine.
/// @param handle Probe. Handle không hợp lệ bị bỏ qua.
void probe3d_remove(context &ctx, probe3d_handle handle);

/// Một lưới ánh sáng gián tiếp (irradiance volume, như của Bevy): njin chụp cảnh ở
/// từng điểm của lưới trong hộp và nhớ ánh sáng tới từ sáu hướng (ambient cube), rồi
/// mọi hình trong hộp (cả vật liệu không `pbr`) lấy ánh sáng nền từ đó thay cho
/// light3d::ambient: góc phòng tối đi, sàn cạnh tường đỏ ửng màu đỏ, chỗ có nắng
/// rọi vào hắt sáng lên trần. Được chụp ở lần vẽ 3D vào thế giới kế tiếp.
struct irradiance3d_desc {
  vec3 center{0.0f, 1.5f, 0.0f}; ///< Tâm hộp.
  vec3 size{8.0f, 3.0f, 8.0f};   ///< Cỡ hộp theo x, y, z.
  /// Số điểm của lưới theo x, 1..16. Mỗi điểm vẽ lại cảnh 6 lần (ở cỡ nhỏ) khi chụp,
  /// chỉ một lần.
  i32 count_x = 4;
  i32 count_y = 2; ///< Số điểm của lưới theo y, 1..16.
  i32 count_z = 4; ///< Số điểm của lưới theo z, 1..16.
  f32 intensity = 1.0f; ///< Độ sáng của ánh sáng lấy từ lưới.
  f32 blend = 0.5f;     ///< Bề dày dải mép hộp mờ dần sang `ambient`, mét.
};

/// Đặt lưới ánh sáng gián tiếp của cảnh (một lưới mỗi lúc; đặt lại thì thay lưới cũ).
/// Nó được chụp ở lần vẽ 3D vào thế giới kế tiếp.
/// @param ctx Context của engine.
/// @param desc Lưới.
void irradiance3d_set(context &ctx, const irradiance3d_desc &desc);

/// Chụp lại lưới ánh sáng ở lần vẽ 3D kế tiếp.
/// @param ctx Context của engine.
void irradiance3d_refresh(context &ctx);

/// Bỏ lưới ánh sáng: các hình lại dùng light3d::ambient.
/// @param ctx Context của engine.
void irradiance3d_clear(context &ctx);

/// Bề mặt của các hình 3D vẽ sau material3d_set().
///
/// Mặc định tô theo Blinn-Phong (`specular`, `shininess`). `pbr` đổi sang cách
/// tô metallic/roughness của glTF, Blender và three.js: nên bật cùng
/// light3d::tonemap, và đặt light3d::environment để kim loại có thứ phản chiếu.
struct material3d {
  f32 specular = 0.25f;  ///< Độ mạnh của điểm sáng bóng. 0 là mặt mờ (gỗ, vải).
  f32 shininess = 32.0f; ///< Độ nhọn của điểm sáng bóng: lớn là mặt nhẵn (nhựa, kim loại).
  /// Màu tự phát sáng, cộng vào sau khi chiếu sáng; `a` là độ mạnh. Bật bloom
  /// (post_fx_set()) để màu này lan sáng ra xung quanh.
  rgba emission{0.0f, 0.0f, 0.0f, 0.0f};
  /// Viền sáng ở mép hình, nơi bề mặt quay đi khỏi camera; `a` là độ mạnh. Làm
  /// vật trông mềm và tách khỏi nền (nhựa, nhân vật hoạt hình).
  rgba rim{0.0f, 0.0f, 0.0f, 0.0f};
  bool unlit = false;        ///< Bỏ qua ánh sáng, giữ nguyên màu (vệt đạn, tia laze, UI trong thế giới).
  /// Ảnh dán lên hình khối (draw_cube3d(), draw_plane3d()...), nhân với màu vẽ.
  /// Không hợp lệ là không dán ảnh. Ảnh đã xếp vào atlas bị bỏ qua.
  texture_handle texture{};
  bool cast_shadows = true;  ///< Đổ bóng khi njin::light3d bật `shadows`.
  /// Opt-in hand-shaped clay normal/albedo variation for SDF draws only.
  /// 0 keeps the original smooth surface. Does not change hit depth or silhouette.
  f32 clay = 0.0f;
  f32 clay_detail = 9.0f; ///< Grain frequency relative to the largest SDF part radius.
  /// Normal map (tangent space, xanh lá hướng lên như glTF, tức OpenGL +Y) cho
  /// hình khối và draw_instanced3d(). Không hợp lệ là không dùng. Với model,
  /// đặt `model_material::normal`.
  texture_handle normal{};
  /// Lớn hơn 0: ảnh (`texture`, `normal`, và ảnh của vật liệu model) dán theo
  /// vị trí trong thế giới thay cho UV của lưới, mỗi ô ảnh `world_uv` đơn vị 3D.
  /// Mặt đứng: u chạy ngang theo mặt, v hướng lên (đỉnh ảnh ở trên), nên vệt
  /// nước chảy thẳng xuống dù hình xoay thế nào; mặt nằm: u theo x, v theo z.
  /// Cho tường và sàn ghép từ nhiều khối: ảnh liền qua các khối, không giãn
  /// theo cỡ khối, và dùng được với lưới không có UV. Mặc định 0: UV của lưới.
  /// Ảnh dán theo cách này được trộn từ hai vị trí lệch nhau theo một nhiễu
  /// chậm, nên các ô ảnh không lộ thành lưới.
  f32 world_uv = 0.0f;
  /// Lớp dưới, chỉ khi `world_uv` > 0: ảnh màu và normal map của thứ nằm dưới
  /// lớp ảnh chính (gạch dưới lớp vữa), lộ ra từng mảng nơi lớp trên đã bong,
  /// nhiều hơn ở chân tường, mép mảng bong hơi tối. Không nhân với màu vẽ.
  /// Lớp dưới chỉ dịch theo bước 1/8 ô, nên hoa văn lưới như gạch (lặp mỗi
  /// 1/8 ô) giữ nguyên mạch. Không hợp lệ là không có lớp dưới.
  texture_handle under{};
  texture_handle under_normal{}; ///< Normal map của lớp dưới, như `normal`.
  /// Phần lớp dưới lộ ra, 0 (không) đến 1 (toàn bộ, lớp trên bong hết).
  f32 under_amount = 0.0f;
  /// Độ phản chiếu, 0..1, cho phản chiếu trên màn hình (`post3d::ssr`): 1 là gương ở
  /// mọi góc nhìn, nhỏ hơn thì chỉ phản chiếu rõ khi nhìn xiên (sàn bóng, mặt nước).
  /// Chỉ có tác dụng khi `post3d::ssr` > 0. Hình SDF (draw_shape3d()) không phản chiếu.
  f32 reflect = 0.0f;

  /// @name PBR (metallic/roughness)
  /// @{

  /// Tô theo vật liệu PBR (BRDF Cook-Torrance GGX) thay cho Blinn-Phong:
  /// `specular` và `shininess` bị bỏ qua, `metallic` và `roughness` thay chúng.
  /// Ánh sáng giữ cùng thang với Blinn-Phong: đèn trắng chiếu thẳng vào mặt trắng
  /// nhám vẫn ra màu của đèn.
  bool pbr = false;
  f32 metallic = 0.0f;  ///< 0 là phi kim (nhựa, gỗ, đá), 1 là kim loại: màu thành màu phản chiếu.
  f32 roughness = 0.5f; ///< Độ nhám, 0 (gương) đến 1 (mờ hẳn).
  /// Ảnh metallic/roughness như glTF: kênh xanh lá nhân vào `roughness`, kênh
  /// xanh dương nhân vào `metallic`. Cho hình khối và draw_instanced3d(); với
  /// model, đặt `model_material::metal_rough` (ảnh trong file glTF được dùng sẵn).
  texture_handle metal_rough{};
  /// Ảnh che khuất môi trường (ambient occlusion), kênh đỏ: chỗ tối nhận ít ánh
  /// sáng nền và môi trường. Như `metal_rough` với model.
  texture_handle occlusion{};
  f32 occlusion_strength = 1.0f; ///< Độ mạnh của `occlusion`, 0..1.
  /// @}

  /// @name Vật liệu PBR nâng cao
  /// Như glTF (KHR_materials_clearcoat, _anisotropy, _transmission, _ior, _volume) và
  /// StandardMaterial của Bevy. Chỉ khi `pbr`; mặc định đều tắt.
  /// @{

  /// Lớp sơn bóng trong suốt phủ trên bề mặt, 0..1 (sơn xe, gỗ đánh vecni, nhựa
  /// bóng): một điểm sáng bóng và phản chiếu thứ hai, sắc, trên lớp vật liệu dưới.
  f32 clearcoat = 0.0f;
  f32 clearcoat_roughness = 0.03f; ///< Độ nhám của lớp sơn bóng, 0..1.
  /// Độ dị hướng, -1..1 (kim loại chải xước, đáy nồi, tóc): điểm sáng bóng kéo dài
  /// thành vệt theo hướng u của ảnh (dương) hay hướng v (âm). Cần lưới có UV.
  f32 anisotropy = 0.0f;
  f32 anisotropy_rotation = 0.0f; ///< Xoay hướng dị hướng trên bề mặt, độ.
  /// Phần ánh sáng xuyên qua vật, 0..1 (kính, nước trong chai, nhựa trong): nhìn qua
  /// vật thấy cảnh phía sau, bị bẻ cong theo `ior` và `thickness`, nhuộm theo
  /// `attenuation_color`, mờ theo `roughness` (kính mờ). Vật xuyên sáng vẽ sau mọi
  /// hình đục, như kính. Cần ảnh của cảnh đục: njin tự sao lại nó ở lần vẽ vào thế
  /// giới (từ frame sau frame đầu tiên có vật xuyên sáng); ở lần vẽ vào render texture
  /// thì vật chỉ trong suốt theo `transmission`, không bẻ cong.
  f32 transmission = 0.0f;
  f32 ior = 1.5f;       ///< Chiết suất: 1.33 nước, 1.5 kính, 2.4 kim cương.
  /// Độ dày của vật, mét: ánh sáng đi trong vật bấy nhiêu nên cảnh sau bị lệch và
  /// nhuộm màu bấy nhiêu. 0 là vật mỏng (tấm kính).
  f32 thickness = 0.0f;
  /// Màu ánh sáng còn lại sau khi đi `attenuation_distance` mét trong vật (rượu vang,
  /// kính màu, nước biển). Trắng là không nhuộm.
  rgba attenuation_color{1.0f, 1.0f, 1.0f, 1.0f};
  f32 attenuation_distance = 0.0f; ///< Mét. 0 là không nhuộm.
  /// Ảnh độ cao (kênh đỏ, trắng là cao) cho parallax: bề mặt phẳng trông lồi lõm thật
  /// khi nhìn xiên (gạch, đá lát, vỏ cây), ảnh dịch theo góc nhìn và chỗ thấp bị chỗ
  /// cao che. Cho hình khối, model và draw_instanced3d(); cần lưới có UV. Không hợp lệ
  /// là không dùng.
  texture_handle height{};
  f32 height_scale = 0.04f; ///< Độ sâu của parallax, theo đơn vị của UV (thường 0.02..0.08).
  /// Ảnh ánh sáng nướng sẵn (lightmap), theo bộ UV thứ hai của lưới (bộ thứ nhất nếu
  /// lưới không có): thay `ambient` (và môi trường) trên bề mặt này bằng ánh sáng gián
  /// tiếp đã tính trước, như lightmap của Bevy và Unity. Ảnh 8 bit coi là sRGB, ảnh
  /// `.hdr` là ánh sáng tuyến tính. Cả vật liệu không `pbr`. Không hợp lệ là không dùng.
  texture_handle lightmap{};
  f32 lightmap_intensity = 1.0f; ///< Độ sáng của `lightmap`.
  /// @}
};

/// Đặt bề mặt cho các hình 3D vẽ sau lệnh này, đến lần gọi tiếp theo hoặc
/// end_3d(). `material3d_set(ctx, {})` là về mặc định. Chỉ shader có sẵn của
/// engine dùng giá trị này.
/// @param ctx Context của engine.
/// @param material Bề mặt.
void material3d_set(context &ctx, const material3d &material);

/// Vẽ hộp đặc, các cạnh song song với trục.
///
/// Như mọi hàm vẽ 3D, chỉ có tác dụng giữa begin_3d() và end_3d(): lệnh vẽ được
/// ghi lại và vẽ thật ở end_3d() (sau khi tính bóng đổ), theo đúng thứ tự gọi. Hình
/// được chiếu sáng theo light3d_set(), light3d_add() và material3d_set(). Nếu game đã bật shader của mình bằng
/// shader_begin(), hình vẽ bằng shader đó; engine đặt sẵn các uniform `vec3`
/// `lightDir`, `lightColor`, `ambient`, `viewPos` (vị trí camera), `fogColor`,
/// uniform `float` `fogDensity` (sương như light3d_set()), uniform `int`
/// `instanceFloats` (0 khi không vẽ instanced), và cho light3d::tonemap: `int`
/// `toneMap` (0 tắt, 1 linear, 2 ACES, 3 AgX, 4 Neutral), `float` `exposure`,
/// `vec3` `lightColorLinear` và `ambientLinear` (màu đèn trong không gian tuyến
/// tính) nếu shader khai báo chúng, và gắn
/// các ảnh phụ đặt bằng shader_set_texture(). Attribute và các uniform `mvp`,
/// `matModel`, `matNormal`, `colDiffuse` theo tên chuẩn của raylib. Vì hình vẽ ở
/// end_3d(), uniform mà game đặt bằng `shader_set_*` lấy giá trị cuối cùng trước
/// end_3d().
/// @param ctx Context của engine.
/// @param center Tâm hộp.
/// @param size Kích thước theo x, y, z.
/// @param color Màu.
void draw_cube3d(const context &ctx, vec3 center, vec3 size, rgba color);

/// Vẽ hình cầu đặc.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param radius Bán kính.
/// @param color Màu.
void draw_sphere3d(const context &ctx, vec3 center, f32 radius, rgba color);

/// Vẽ mặt phẳng nằm ngang (song song mặt xz), mặt trên hướng `+y`.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param size Kích thước theo x và z.
/// @param color Màu.
void draw_plane3d(const context &ctx, vec3 center, vec2 size, rgba color);

/// Vẽ hình trụ đặc nối hai điểm (lưới tam giác). Dùng cho vệt đạn, dây, cột.
/// @param ctx Context của engine.
/// @param from Tâm đáy thứ nhất.
/// @param to Tâm đáy thứ hai.
/// @param radius Bán kính.
/// @param color Màu.
void draw_cylinder3d(const context &ctx, vec3 from, vec3 to, f32 radius, rgba color);

/// Vẽ viên nang (capsule) đặc bằng lưới tam giác: hình trụ nối hai điểm, hai đầu
/// tròn. Nối được hai điểm bất kỳ; cần viền tròn mịn khi nhìn gần thì dùng
/// draw_shape3d() với njin::shape3d_capsule.
/// @param ctx Context của engine.
/// @param from Tâm nửa cầu thứ nhất.
/// @param to Tâm nửa cầu thứ hai.
/// @param radius Bán kính.
/// @param color Màu.
void draw_capsule3d(const context &ctx, vec3 from, vec3 to, f32 radius, rgba color);

/// Loại hình của njin::shape3d.
enum shape3d_kind {
  shape3d_sphere,   ///< Hình cầu: `radius`.
  shape3d_box,      ///< Hộp: `size`, bo cạnh theo `rounding`.
  shape3d_capsule,  ///< Viên nang dọc trục y: `radius`, `height` (cả hai đầu tròn).
  shape3d_cylinder, ///< Hình trụ dọc trục y: `radius`, `height`, bo cạnh theo `rounding`.
  shape3d_torus,    ///< Hình xuyến nằm trên mặt xz: `radius` (tới tâm ống), `thickness` (bán kính ống).
};

/// Một hình cơ bản vẽ bằng SDF (hàm khoảng cách có dấu), dùng với draw_shape3d().
///
/// Khác draw_sphere3d() hay draw_capsule3d() (lưới tam giác, thấy cạnh khi nhìn
/// gần), hình SDF được tính trên từng điểm ảnh nên viền luôn tròn mịn ở mọi cỡ, và
/// bo góc được. Vẫn nhận ánh sáng, bóng đổ (cả đổ lẫn nhận), njin::material3d và
/// njin::fx3d như hình khác. Tốn hơn một hình lưới, nên dùng cho nhân vật và vật
/// cần đẹp gần camera, không phải hàng nghìn viên đạn.
struct shape3d {
  shape3d_kind kind = shape3d_sphere; ///< Loại hình.
  vec3 position{0.0f, 0.0f, 0.0f};    ///< Tâm hình.
  vec3 rotation{0.0f, 0.0f, 0.0f};    ///< Góc xoay, độ, cùng thứ tự với njin::transform3d.
  vec3 size{1.0f, 1.0f, 1.0f};        ///< Kích thước của hộp theo x, y, z.
  f32 radius = 0.5f;                  ///< Bán kính (cầu, viên nang, trụ) hoặc bán kính vòng (xuyến).
  f32 height = 1.0f;                  ///< Chiều cao cả hình theo y (viên nang, trụ).
  f32 thickness = 0.15f;              ///< Bán kính ống của hình xuyến.
  f32 rounding = 0.0f;                ///< Bán kính bo cạnh (hộp, trụ). 0 là cạnh sắc.
};

/// Vẽ một hình SDF mịn. Như các hình khác, chỉ có tác dụng giữa begin_3d() và
/// end_3d(), theo material3d_set() và fx3d_set() lúc gọi. Luôn vẽ bằng shader của
/// engine (shader của game bật bằng shader_begin() không áp dụng), và không dán ảnh.
///
/// @code
/// // Nhân vật hình viên nang, bóng như nhựa, viền sáng xanh nhạt.
/// njin::material3d_set(ctx, {.specular = 0.6f, .shininess = 60.0f, .rim = {0.55f, 0.75f, 1.0f, 0.45f}});
/// njin::draw_shape3d(ctx, {.kind = njin::shape3d_capsule, .position = pos, .radius = 0.28f, .height = 0.9f},
///                    njin::colors::blue);
/// njin::material3d_set(ctx, {});
/// @endcode
/// @param ctx Context của engine.
/// @param shape Hình.
/// @param color Màu.
void draw_shape3d(const context &ctx, const shape3d &shape, rgba color);

/// Một phần của hình ghép SDF (draw_sdf_blend()): hình nón bo tròn nối `a` với
/// `b`, bán kính `ra` ở `a` và `rb` ở `b` (bằng nhau là viên nang, `a` trùng `b`
/// là hình cầu).
struct sdf_part {
  vec3 a{0.0f, 0.0f, 0.0f}; ///< Tâm đầu thứ nhất.
  vec3 b{0.0f, 0.0f, 0.0f}; ///< Tâm đầu thứ hai.
  f32 ra = 0.1f;            ///< Bán kính ở `a`.
  f32 rb = 0.1f;            ///< Bán kính ở `b`.
  /// Độ mềm chỗ phần này nối vào các phần đứng trước nó trong danh sách, đơn vị
  /// thế giới. Âm (mặc định) là dùng `blend` của draw_sdf_blend(). Cho phép nối
  /// mềm chỗ cần đắp (vai vào thân) mà vẫn thon chỗ không muốn phình (khuỷu tay,
  /// đầu gối: đặt nhỏ, vì smooth min đắp thêm khoảng `blend / 4` ở chỗ nối).
  f32 blend = -1.0f;
};

/// Số phần tối đa của một hình ghép SDF.
inline constexpr u32 sdf_blend_max = 32;

/// Vẽ nhiều phần SDF hòa làm một khối liền (smooth min): chỗ hai phần gặp nhau
/// được đắp đầy mượt thay vì gãy góc, như nặn bằng đất sét. Nhân vật ghép từ đầu,
/// cổ, thân và các đoạn tay chân, rồi đặt lại các phần mỗi frame theo tư thế,
/// vẫn liền mạch khi cử động.
///
/// Như draw_shape3d(): tính trên từng điểm ảnh nên viền mịn ở mọi cỡ, nhận ánh
/// sáng, đổ và nhận bóng, theo material3d_set() và fx3d_set() lúc gọi. Mỗi hình
/// ghép là một lệnh vẽ; tốn theo số điểm ảnh nó phủ nhân số phần.
///
/// @code
/// // Một cánh tay: bắp tay, cẳng tay, bàn tay nối liền.
/// const njin::sdf_part arm[] = {{.a = shoulder, .b = elbow, .ra = 0.05f, .rb = 0.04f},
///                               {.a = elbow, .b = wrist, .ra = 0.04f, .rb = 0.03f},
///                               {.a = wrist, .b = fingers, .ra = 0.035f, .rb = 0.02f}};
/// njin::draw_sdf_blend(ctx, arm, 3, 0.03f, clay);
/// @endcode
/// @param ctx Context của engine.
/// @param parts Các phần; được chép lại, không cần sống sau lệnh gọi.
/// @param count Số phần, 1 đến njin::sdf_blend_max (phần thừa bị bỏ, kèm cảnh báo).
/// @param blend Độ mềm của chỗ nối, đơn vị thế giới, cho các phần không đặt
/// `sdf_part::blend` riêng: 0 là nối cứng, lớn thì phần này chảy sang phần kia xa hơn.
/// @param color Màu.
void draw_sdf_blend(const context &ctx, const sdf_part *parts, u32 count, f32 blend, rgba color);

/// Nạp model 3D từ file glTF (`.glb`, `.gltf`) hoặc OBJ.
///
/// Đường dẫn tính như texture_load(). Màu và texture của vật liệu trong file
/// được giữ, và model được chiếu sáng theo light3d_set().
/// @param ctx Context của engine.
/// @param path Đường dẫn file.
/// @return Handle của model, hoặc handle không hợp lệ nếu file thiếu hay lỗi.
model_handle model_load(context &ctx, const char *path);

/// Cách model_load() nạp một phần của file glTF: bỏ hay chỉ giữ một số node,
/// và gộp các mesh cùng vật liệu thành một.
///
/// Tên node so theo chuỗi con: `"substrate"` khớp node `PBK_substrate.001`.
/// Node không có mesh không bị ảnh hưởng. Với file OBJ thì bộ lọc bị bỏ qua.
struct model_load_desc {
  const char *path = nullptr;               ///< Đường dẫn file, như model_load().
  const char *const *skip_nodes = nullptr;  ///< Bỏ mesh của node có tên chứa một trong các chuỗi này.
  u32 skip_count = 0;                       ///< Số chuỗi trong `skip_nodes`.
  const char *const *only_nodes = nullptr;  ///< Nếu có: chỉ giữ mesh của node có tên chứa một trong các chuỗi này.
  u32 only_count = 0;                       ///< Số chuỗi trong `only_nodes`.
  /// Gộp các mesh dùng cùng vật liệu thành một mesh (mỗi mesh tối đa 65535 đỉnh),
  /// để draw_instanced3d() vẽ ít lệnh hơn. Mesh gộp bỏ dữ liệu xương: model vẽ ở
  /// tư thế gốc trong file, như đồ vật đứng yên. Morph target được giữ.
  bool merge = false;
  /// Bật `surface.pbr` cho mọi vật liệu của file: tô theo metallic/roughness của
  /// glTF, với ảnh metallic/roughness và occlusion trong file (nạp lần đầu vẽ).
  bool pbr = false;
};

/// Nạp model như model_load(), với bộ lọc node và gộp mesh của `desc`.
///
/// Vật liệu glTF có `KHR_materials_transmission` (kính trong) được vẽ trong
/// suốt: độ mờ theo `transmissionFactor`, không đổ bóng, vẽ sau mọi phần đục.
///
/// @code
/// // Khung cửa sổ không có mảng tường của nó, gộp theo vật liệu.
/// const char *skip[] = {"substrate", "floor_band"};
/// const njin::model_handle frame =
///     njin::model_load(ctx, {.path = "assets/window.glb", .skip_nodes = skip, .skip_count = 2, .merge = true});
/// @endcode
/// @param ctx Context của engine.
/// @param desc File, bộ lọc node và có gộp mesh hay không.
/// @return Handle của model, hoặc handle không hợp lệ nếu file thiếu, lỗi, hay bộ lọc không giữ mesh nào.
model_handle model_load(context &ctx, const model_load_desc &desc);

/// Giải phóng model cùng các texture của nó. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Model cần giải phóng.
void model_unload(context &ctx, model_handle handle);

/// Một lưới tam giác do game tự dựng, cho model_create(): địa hình sinh theo
/// seed, hình ghép lúc chạy. Các con trỏ chỉ cần sống đến khi model_create() trả về.
struct mesh3d_data {
  const vec3 *positions = nullptr; ///< Vị trí `vertex_count` đỉnh.
  /// Pháp tuyến từng đỉnh (độ dài 1). nullptr thì engine tự tính: mỗi đỉnh lấy
  /// trung bình các tam giác chung nó, nên mặt cong trông mịn.
  const vec3 *normals = nullptr;
  const rgba *colors = nullptr; ///< Màu từng đỉnh, nhân với màu vật liệu. nullptr là trắng.
  /// Toạ độ ảnh (UV) từng đỉnh, cho ảnh của vật liệu (model_material_set()):
  /// một mảnh cắt từ lưới đã có UV giữ nguyên ảnh của nó. nullptr là (0, 0)
  /// mọi đỉnh, tức chỉ có màu.
  const vec2 *texcoords = nullptr;
  u32 vertex_count = 0;         ///< Số đỉnh.
  /// Ba chỉ số một tam giác, ngược chiều kim đồng hồ khi nhìn từ mặt trước.
  /// nullptr thì mỗi ba đỉnh liên tiếp là một tam giác. Có chỉ số thì tối đa
  /// 65535 đỉnh (chỉ số 16 bit của raylib): chia lưới lớn thành nhiều model.
  const u32 *indices = nullptr;
  u32 index_count = 0; ///< Số chỉ số, bội của 3.
};

/// Tạo model từ một lưới tam giác trong bộ nhớ, rồi dùng như model nạp từ file:
/// draw_model(), draw_instanced3d(), model_material_set(), ray3d_model(),
/// model_unload(). Chiếu sáng, đổ bóng và nhận bóng như mọi hình khác.
///
/// @code
/// // Một tam giác đỏ nằm trên mặt đất.
/// const njin::vec3 p[] = {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}};
/// const njin::rgba c[] = {njin::colors::red, njin::colors::red, njin::colors::red};
/// const njin::model_handle tri = njin::model_create(ctx, {.positions = p, .colors = c, .vertex_count = 3});
/// @endcode
/// @param ctx Context của engine.
/// @param mesh Lưới.
/// @return Handle của model, hoặc handle không hợp lệ nếu lưới rỗng hay sai (cảnh báo nói vì sao).
model_handle model_create(context &ctx, const mesh3d_data &mesh);

/// Một lưới có xương do game tự ghép lúc chạy, cho model_create_skinned():
/// nhân vật ghép từ nhiều mảnh (đầu, thân, tay, tóc...) theo một bộ gen, mà
/// không xuất sẵn một file cho mỗi tổ hợp. Các con trỏ chỉ cần sống đến khi
/// model_create_skinned() trả về.
struct skinned_mesh3d_data {
  const vec3 *positions = nullptr; ///< Vị trí `vertex_count` đỉnh, ở tư thế gốc của `skeleton`.
  /// Pháp tuyến từng đỉnh (độ dài 1). nullptr thì engine tự tính như model_create().
  const vec3 *normals = nullptr;
  /// Toạ độ ảnh (UV) từng đỉnh, cho ảnh của vật liệu (model_material_set()):
  /// nhân vật có texture riêng vẫn dùng chung thư viện clip. nullptr là (0, 0)
  /// mọi đỉnh, tức chỉ có màu.
  const vec2 *texcoords = nullptr;
  u32 vertex_count = 0; ///< Số đỉnh.
  /// Bốn xương mỗi đỉnh: chỉ số xương của `skeleton` (model_bone_find()), nên
  /// tối đa 255 xương.
  const u8 *joints = nullptr;
  const f32 *weights = nullptr; ///< Bốn trọng số mỗi đỉnh, cùng thứ tự `joints`, cộng lại bằng 1.
  /// Ba chỉ số một tam giác, ngược chiều kim đồng hồ khi nhìn từ mặt trước.
  const u32 *indices = nullptr;
  u32 index_count = 0; ///< Số chỉ số, bội của 3.
  /// Chia tam giác thành các phần, mỗi phần một vật liệu (vật liệu `k` là phần
  /// `k`, màu trắng): chỉ số kết thúc của từng phần trong `indices`, tăng dần,
  /// phần cuối kết thúc ở `index_count`. nullptr là một phần. Mỗi phần tối đa
  /// 65535 đỉnh.
  const u32 *part_ends = nullptr;
  u32 part_count = 0; ///< Số phần trong `part_ends`.
  /// Model có xương đã nạp bằng model_load(): model mới chép bộ xương và tư
  /// thế gốc của nó, và **dùng chung** các animation của nó (không chép), nên
  /// model_anim_find(), draw_model_anim() và model_bone_pose() trên model mới
  /// chạy các clip của model này. Giải phóng model này trước thì model mới chỉ
  /// còn tư thế gốc.
  model_handle skeleton{};
};

/// Tạo model có xương từ một lưới trong bộ nhớ, dùng bộ xương và animation
/// của model khác (một thư viện clip nạp một lần cho mọi nhân vật). Vẽ bằng
/// draw_model_anim() như model nạp từ file, mỗi lần vẽ một tư thế và (với
/// njin::model_recolor) một bảng màu riêng, nên nhiều nhân vật dùng chung một
/// lưới mà vẫn mỗi người một động tác, một bộ quần áo.
///
/// @code
/// // Thư viện clip một lần; mỗi tổ hợp mảnh ghép một model.
/// const njin::model_handle clips = njin::model_load(ctx, "assets/shared_animations.glb");
/// const njin::model_handle body = njin::model_create_skinned(
///     ctx, {.positions = pos.data(), .vertex_count = n, .joints = joints.data(), .weights = weights.data(),
///           .indices = idx.data(), .index_count = (njin::u32)idx.size(), .skeleton = clips});
/// njin::draw_model_anim(ctx, body, at, {.anim = njin::model_anim_find(ctx, body, "Walk_Loop"), .time = t});
/// @endcode
/// @param ctx Context của engine.
/// @param mesh Lưới, các phần và model cho bộ xương.
/// @return Handle của model, hoặc handle không hợp lệ nếu lưới sai hay `skeleton`
/// không phải model có xương (cảnh báo nói vì sao). Giải phóng bằng model_unload().
model_handle model_create_skinned(context &ctx, const skinned_mesh3d_data &mesh);

/// Cách model_lod_build() làm các mức chi tiết (LOD) của một model.
struct model_lod_desc {
  i32 levels = 3;        ///< Số mức giản lược, 1..4. Mức k giữ khoảng `ratio` mũ k số tam giác.
  f32 ratio = 0.5f;      ///< Phần tam giác mỗi mức giữ lại so với model gốc, lũy thừa theo mức, 0.1..0.9.
  /// Hình được lệch tối đa bao nhiêu, tính theo cỡ của mesh (0.05 là 5%). Giản
  /// lược dừng ở đây dù chưa đạt `ratio`, nên model ít tam giác có thể ít mức hơn.
  f32 max_error = 0.05f;
  /// Mức 1 được vẽ khi model (quả cầu bao nó) cao chưa tới phần này của chiều cao
  /// màn hình; mỗi mức sau ở một nửa mức trước.
  f32 screen = 0.25f;
};

/// Tạo các mức chi tiết cho model: bản giản lược, ít tam giác hơn, được vẽ thay
/// model khi nó nhỏ trên màn hình. Sau đó draw_model(), draw_model_anim() và
/// njin::model3d tự chọn mức theo khoảng cách tới camera; model có xương giữ
/// xương và animation ở mọi mức, model có morph giữ morph ở mọi mức.
/// draw_instanced3d() và ray3d_model() luôn dùng model gốc.
///
/// Làm một lần sau khi nạp (tốn vài mili giây với vài chục nghìn tam giác); gọi
/// lại thì thay các mức cũ. model_unload() giải phóng cả các mức.
///
/// @code
/// const njin::model_handle tree = njin::model_load(ctx, "tree.glb");
/// njin::model_lod_build(ctx, tree, {.levels = 2});
/// @endcode
/// @param ctx Context của engine.
/// @param handle Model từ model_load() hay model_create().
/// @param desc Số mức và mức giản lược.
/// @return Số mức đã tạo, 0 nếu model quá đơn giản để giản lược hay handle không hợp lệ.
i32 model_lod_build(context &ctx, model_handle handle, const model_lod_desc &desc = {});

/// Vị trí, hướng và tỉ lệ của một vật 3D.
///
/// Thứ tự áp dụng: phóng theo `scale`, xoay quanh `z` (roll), rồi quanh `x`
/// (pitch), rồi quanh `y` (yaw), cuối cùng dời đến `position`. Đây cũng là cách
/// hướng của camera góc nhìn thứ nhất được ghép từ yaw và pitch.
struct transform3d {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Vị trí.
  vec3 rotation{0.0f, 0.0f, 0.0f}; ///< Góc xoay quanh x, y, z, tính bằng độ.
  vec3 scale{1.0f, 1.0f, 1.0f};    ///< Tỉ lệ theo x, y, z.
};

/// Vật liệu của một phần model: mỗi file glTF/OBJ có một hay nhiều vật liệu, mỗi
/// mesh dùng một. Đọc bằng model_material_get(), sửa rồi đặt lại bằng
/// model_material_set().
///
/// Texture để trống (handle không hợp lệ) nghĩa là giữ texture trong file (nếu có).
/// Normal map không cần tangent trong file: shader tự dựng từ đạo hàm màn hình.
struct model_material {
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu gốc, nhân với ảnh albedo. Mặc định là màu trong file.
  texture_handle albedo{};   ///< Ảnh màu.
  texture_handle normal{};   ///< Normal map (tangent space, xanh lá hướng lên như glTF).
  texture_handle emission{}; ///< Ảnh phát sáng, nhân với `emission_color`.
  rgba emission_color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nhân vào ảnh phát sáng (của file hay `emission`).
  /// Ảnh metallic/roughness (như material3d::metal_rough) thay cho ảnh của file,
  /// khi `surface.pbr` bật. Không hợp lệ là ảnh trong file glTF nếu có.
  texture_handle metal_rough{};
  /// Ảnh che khuất môi trường (như material3d::occlusion) thay cho ảnh của file.
  texture_handle occlusion{};
  /// Shader riêng cho phần này, như shader_begin() với hình khối, cả khi model
  /// vẽ bằng draw_instanced3d() (shader đọc `instanceFloats` để biết mình đang
  /// vẽ kiểu nào). Không hợp lệ là shader có sẵn của engine.
  shader_handle shader{};
  /// Độ bóng, phát sáng, unlit, đổ bóng, `world_uv`, PBR. `surface.texture`,
  /// `surface.normal`, `surface.metal_rough` và `surface.occlusion` không dùng ở
  /// đây (đã có `albedo`, `normal`, `metal_rough`, `occlusion`). Với file glTF,
  /// `surface.metallic`, `surface.roughness` và `surface.occlusion_strength` lấy
  /// theo vật liệu trong file; `surface.pbr` tắt trừ khi model_load_desc::pbr.
  material3d surface{};
  /// Vẽ cả hai mặt của mỗi tam giác (không bỏ mặt sau), cho lá cây, giấy, vải
  /// mỏng một lớp. Mặc định theo `doubleSided` của vật liệu trong file glTF.
  bool double_sided = false;
};

/// Số vật liệu của model.
/// @param ctx Context của engine.
/// @param handle Model.
/// @return Số vật liệu, 0 nếu handle không hợp lệ.
i32 model_material_count(const context &ctx, model_handle handle);

/// Tìm vật liệu theo tên đặt trong file glTF.
///
/// So theo tiền tố: `"Human_Shirt"` khớp vật liệu `Human_Shirt_53_19_16`.
/// @param ctx Context của engine.
/// @param handle Model.
/// @param name Tên hoặc phần đầu của tên.
/// @return Chỉ số của vật liệu đầu tiên khớp, hoặc -1 nếu không có (hay file không phải glTF).
i32 model_material_find(const context &ctx, model_handle handle, const char *name);

/// Vật liệu thứ `index` của model.
/// @param ctx Context của engine.
/// @param handle Model.
/// @param index 0..model_material_count() - 1.
/// @return Vật liệu, hoặc mặc định nếu handle hay `index` không hợp lệ.
model_material model_material_get(const context &ctx, model_handle handle, i32 index);

/// Đặt vật liệu thứ `index` của model, cho mọi lần vẽ sau đó.
///
/// @code
/// njin::model_material m = njin::model_material_get(ctx, crate, 0);
/// m.albedo = njin::texture_load(ctx, "assets/crate_wood.png");
/// m.normal = njin::texture_load(ctx, "assets/crate_wood_n.png");
/// m.surface.specular = 0.1f;
/// njin::model_material_set(ctx, crate, 0, m);
/// @endcode
/// @param ctx Context của engine.
/// @param handle Model.
/// @param index 0..model_material_count() - 1, hoặc -1 cho mọi vật liệu.
/// @param material Vật liệu mới.
void model_material_set(context &ctx, model_handle handle, i32 index, const model_material &material);

/// Vẽ model tại `transform`, với vật liệu của nó (model_material_set()).
/// material3d_set() không áp dụng cho model; fx3d_set() thì có. Shader của game
/// bật bằng shader_begin() thay shader của mọi phần không có shader riêng.
///
/// Model nằm ngoài tầm nhìn camera (hộp bao của nó ngoài frustum) không được vẽ
/// nhưng vẫn đổ bóng vào cảnh; render_info_get() đếm số model bị bỏ. Một lưới lớn
/// (địa hình, đường sá) nên chia thành nhiều model theo vùng để phần khuất được
/// bỏ. Có mức chi tiết (model_lod_build()) thì model ở xa vẽ bằng mức đơn giản hơn.
/// @param ctx Context của engine.
/// @param handle Model từ model_load(). Handle không hợp lệ bị bỏ qua.
/// @param transform Vị trí, hướng và tỉ lệ.
/// @param tint Màu nhân vào màu của model. Trắng là giữ nguyên.
void draw_model(const context &ctx, model_handle handle, const transform3d &transform,
                rgba tint = colors::white);

/// Số animation xương trong file của model (glTF có skin). model_load() nạp chúng
/// cùng model. Model không có skin nhưng có morph target thì animation của nó là
/// các clip có đường cong trọng số (model_morph_count()).
/// @param ctx Context của engine.
/// @param handle Model.
/// @return Số animation, 0 nếu model không có hay handle không hợp lệ.
i32 model_anim_count(const context &ctx, model_handle handle);

/// Tìm animation theo tên đặt trong Blender (tên action khi xuất glTF).
/// @param ctx Context của engine.
/// @param handle Model.
/// @param name Tên animation.
/// @return Chỉ số 0..model_anim_count() - 1, hoặc -1 nếu không có.
i32 model_anim_find(const context &ctx, model_handle handle, const char *name);

/// Tên của animation thứ `index`.
/// @param ctx Context của engine.
/// @param handle Model.
/// @param index 0..model_anim_count() - 1.
/// @return Tên, hoặc chuỗi rỗng nếu handle hay `index` không hợp lệ.
const char *model_anim_name(const context &ctx, model_handle handle, i32 index);

/// Độ dài của animation thứ `index`, giây.
/// @param ctx Context của engine.
/// @param handle Model.
/// @param index 0..model_anim_count() - 1.
/// @return Độ dài, 0 nếu handle hay `index` không hợp lệ.
f32 model_anim_duration(const context &ctx, model_handle handle, i32 index);

struct bone_pose3d;

/// Tư thế của một model có xương khi vẽ: animation nào, ở giây thứ mấy, và
/// (tùy chọn) trộn với animation thứ hai để chuyển mượt giữa hai động tác.
/// Hoặc game tự đặt từng xương bằng `bones` (ragdoll3d_bones()).
struct model_pose {
  i32 anim = -1;           ///< Animation (model_anim_find()). -1 là tư thế gốc trong file.
  f32 time = 0.0f;         ///< Thời điểm trong animation, giây.
  bool loop = true;        ///< Lặp lại; không thì dừng ở khung cuối.
  i32 blend_anim = -1;     ///< Animation thứ hai để trộn vào. -1 là không trộn.
  f32 blend_time = 0.0f;   ///< Thời điểm trong animation thứ hai, giây.
  bool blend_loop = true;  ///< Lặp animation thứ hai.
  f32 blend = 0.0f;        ///< Tỉ lệ trộn: 0 chỉ có `anim`, 1 chỉ có `blend_anim`.
  /// Tư thế do game đặt: model_bone_count() xương, trong không gian của model
  /// như model_bone_pose() trả về (ragdoll3d_bones() điền mảng này). Khác
  /// nullptr thì các trường animation ở trên bị bỏ qua khi đặt xương (chúng vẫn
  /// điều khiển trọng số morph, xem `morph_weights`). Engine đọc mảng ngay lúc
  /// vẽ (draw_model_anim(), hay khi vẽ entity có njin::model3d), không giữ lại.
  const bone_pose3d *bones = nullptr;
  /// Trọng số morph target (blend shape) do game đặt, theo chỉ số
  /// model_morph_find(): **cộng thêm** vào trọng số mặc định của file và của
  /// animation đang chạy (`anim`, `blend_anim`), nên game nháy mắt trong khi
  /// animation cử động miệng. nullptr là không thêm gì. Engine đọc mảng ngay lúc
  /// vẽ, không giữ lại.
  const f32 *morph_weights = nullptr;
  i32 morph_count = 0; ///< Số phần tử của `morph_weights`.
};

/// Vẽ model có xương ở tư thế `pose`. Như draw_model(), và bóng đổ theo đúng
/// tư thế. Mỗi lần vẽ có tư thế riêng, nên cùng một model vẽ nhiều lần (đám
/// quái) mỗi con một động tác được.
///
/// Xương được tính trên GPU, tối đa 128 xương, 4 xương mỗi đỉnh (giới hạn của
/// glTF). Phần model vẽ bằng shader của game (`model_material::shader` hay
/// shader_begin()), draw_instanced3d() và ray3d_model() dùng tư thế gốc.
///
/// @code
/// // Chạy khi di chuyển, đứng yên khi dừng, chuyển mượt trong 0.2 giây.
/// blend = move_toward(blend, moving ? 1.0f : 0.0f, dt / 0.2f);
/// njin::draw_model_anim(ctx, hero, {.position = pos, .rotation = {0, yaw, 0}},
///                       {.anim = idle, .time = t, .blend_anim = run, .blend_time = t, .blend = blend});
/// @endcode
/// @param ctx Context của engine.
/// @param handle Model từ model_load(). Handle không hợp lệ bị bỏ qua.
/// @param transform Vị trí, hướng và tỉ lệ.
/// @param pose Tư thế. Model không có xương thì vẽ như draw_model().
/// @param tint Màu nhân vào màu của model.
void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint = colors::white);

/// Màu thay cho màu gốc của một vật liệu, chỉ trong một lần vẽ.
struct model_recolor {
  i32 material = -1;                 ///< Vật liệu (model_material_find()). -1 là không đổi gì.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu thay cho njin::model_material::color; độ trong suốt giữ của vật liệu.
};

/// Như draw_model_anim() ở trên, và vật liệu `recolor.material` mang màu
/// `recolor.color` trong lần vẽ này.
///
/// Để nhiều bản vẽ của cùng một model mỗi bản một màu áo (đồng phục của từng
/// phe) mà không phải nạp model nhiều lần hay đổi model_material_set() giữa
/// các lần vẽ: các lần vẽ chỉ chạy ở end_3d(), nên model_material_set() ở giữa
/// chúng không có tác dụng riêng cho từng lần.
/// @code
/// const njin::i32 shirt = njin::model_material_find(ctx, man, "Shirt");
/// njin::draw_model_anim(ctx, man, at, pose, njin::colors::white, {.material = shirt, .color = team_red});
/// @endcode
/// @param ctx Context của engine.
/// @param handle Model từ model_load().
/// @param transform Vị trí, hướng và tỉ lệ.
/// @param pose Tư thế.
/// @param tint Màu nhân vào màu của cả model (sau khi đổi màu vật liệu).
/// @param recolor Vật liệu đổi màu và màu mới.
void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint, const model_recolor &recolor);

/// Như draw_model_anim() ở trên, với nhiều vật liệu đổi màu một lúc: bảng màu
/// riêng của một nhân vật (da, tóc, áo, quần) trên một model dùng chung.
/// @code
/// const njin::model_recolor palette[] = {{.material = 0, .color = skin}, {.material = 1, .color = shirt}};
/// njin::draw_model_anim(ctx, body, at, pose, njin::colors::white, palette, 2);
/// @endcode
/// @param ctx Context của engine.
/// @param handle Model.
/// @param transform Vị trí, hướng và tỉ lệ.
/// @param pose Tư thế.
/// @param tint Màu nhân vào màu của cả model (sau khi đổi màu vật liệu).
/// @param recolors Các vật liệu đổi màu; một vật liệu có mặt hai lần lấy màu đầu tiên.
/// @param count Số phần tử của `recolors`.
void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint, const model_recolor *recolors, u32 count);

/// Số xương của model (0 nếu không có xương hay handle không hợp lệ).
/// @param ctx Context của engine.
/// @param handle Model.
/// @return Số xương.
i32 model_bone_count(const context &ctx, model_handle handle);

/// Tên một xương, như trong file glTF.
/// @param ctx Context của engine.
/// @param handle Model.
/// @param bone 0..model_bone_count() - 1.
/// @return Tên, hoặc chuỗi rỗng nếu `bone` hay handle không hợp lệ.
const char *model_bone_name(const context &ctx, model_handle handle, i32 bone);

/// Tìm xương theo tên (so đúng cả chuỗi).
/// @param ctx Context của engine.
/// @param handle Model.
/// @param name Tên xương.
/// @return Chỉ số 0..model_bone_count() - 1, hoặc -1 nếu không có.
i32 model_bone_find(const context &ctx, model_handle handle, const char *name);

/// Xương cha của `bone` trong bộ xương, để đi lên chuỗi xương (IK: đùi, cẳng
/// chân, bàn chân) hay biết xương nào nằm dưới xương nào.
/// @param ctx Context của engine.
/// @param handle Model.
/// @param bone 0..model_bone_count() - 1.
/// @return Chỉ số xương cha, hoặc -1 với xương gốc, handle hay `bone` không hợp lệ.
i32 model_bone_parent(const context &ctx, model_handle handle, i32 bone);

/// Hộp bao phần da mà xương `bone` kéo mạnh nhất (cả các xương con nếu
/// `children`), ở tư thế gốc trong file: theo các trục của model, tính từ gốc
/// của xương. Theo trục của model nên hộp ôm sát (đế bàn chân nằm phẳng trên
/// sàn), không phình ra như hộp dựng theo một xương nằm chéo. Để đặt collider
/// khớp với hình thật (bàn chân, bàn tay, đầu): khi vẽ, xoay hộp theo đúng góc
/// xương đã xoay so với tư thế gốc (model_bone_pose() với tư thế gốc).
///
/// @code
/// // Bàn chân (không kể ngón), quanh gốc xương foot_l.
/// njin::vec3 lo, hi;
/// if (njin::model_bone_bounds(ctx, man, foot_l, false, &lo, &hi)) { ... }
/// @endcode
/// @param ctx Context của engine.
/// @param handle Model có da (lưới có xương).
/// @param bone 0..model_bone_count() - 1.
/// @param children Tính cả da của mọi xương nằm dưới `bone`.
/// @param min Nhận góc nhỏ nhất của hộp.
/// @param max Nhận góc lớn nhất.
/// @return `false` (min, max giữ nguyên) nếu không có đỉnh da nào, model không
/// có da hay handle, `bone` không hợp lệ.
bool model_bone_bounds(const context &ctx, model_handle handle, i32 bone, bool children, vec3 *min, vec3 *max);

/// Thể tích chất liệu của model, đơn vị thế giới khối, ở tư thế gốc: tổng thể
/// tích mà các mặt tam giác của nó bao kín (vỏ rỗng chỉ tính phần thành). Nhân
/// với khối lượng riêng của vật liệu là được khối lượng của vật, để mỗi vật nặng
/// theo đúng hình của nó (thanh rỗng nhẹ hơn thanh đặc cùng cỡ).
/// @code
/// // Thanh nhôm: 2700 kg mỗi mét khối.
/// const float mass = njin::model_volume(ctx, bar) * 2700.0f;
/// @endcode
/// @param ctx Context của engine.
/// @param handle Model từ model_load() hay model_create().
/// @return Thể tích; lưới hở thì gần đúng. 0 nếu handle không hợp lệ.
f32 model_volume(const context &ctx, model_handle handle);

/// Hộp bao của model ở tư thế gốc, đơn vị thế giới, trước transform của lần vẽ:
/// để biết cỡ thật của model nạp từ file (đặt nó lên mặt đất, phóng cho đúng cỡ,
/// dựng hộp va chạm).
/// @code
/// njin::vec3 lo, hi;
/// njin::model_bounds(ctx, crate, &lo, &hi);
/// const float scale = 0.6f / (hi.y - lo.y); // thùng cao 0.6 m
/// @endcode
/// @param ctx Context của engine.
/// @param handle Model từ model_load() hay model_create().
/// @param min Nhận góc nhỏ nhất. Có thể nullptr.
/// @param max Nhận góc lớn nhất. Có thể nullptr.
/// @return false nếu handle không hợp lệ (khi đó `min`, `max` không đổi).
bool model_bounds(const context &ctx, model_handle handle, vec3 *min, vec3 *max);

/// Các đỉnh của phần da đó (như model_bone_bounds()), cùng hệ trục: theo các
/// trục của model ở tư thế gốc, tính từ gốc của xương. Để dựng một khối lồi ôm
/// đúng hình (physics3d_hull_create()).
/// @param ctx Context của engine.
/// @param handle Model có da.
/// @param bone 0..model_bone_count() - 1.
/// @param children Tính cả da của mọi xương nằm dưới `bone`.
/// @param out Mảng nhận các đỉnh, hoặc nullptr để chỉ đếm.
/// @param count Số phần tử của `out`.
/// @return Số đỉnh có (có thể lớn hơn `count`: chỉ `count` đỉnh đầu được ghi).
i32 model_bone_points(const context &ctx, model_handle handle, i32 bone, bool children, vec3 *out, i32 count);

/// Một xương ở một tư thế: vị trí và ba trục của nó trong không gian của
/// model (trước transform của lần vẽ), như draw_model_anim() đặt nó.
struct bone_pose3d {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Gốc của xương.
  vec3 x_axis{1.0f, 0.0f, 0.0f};   ///< Trục x của xương, độ dài 1.
  vec3 y_axis{0.0f, 1.0f, 0.0f};   ///< Trục y (dọc theo xương với rig của Blender), độ dài 1.
  vec3 z_axis{0.0f, 0.0f, 1.0f};   ///< Trục z, độ dài 1.
};

/// Số morph target (blend shape) của model: các target của glTF (biểu cảm mặt,
/// cơ phồng, cửa mở), theo tên. Target cùng tên ở nhiều mesh (cái nháy mắt ở
/// mặt và ở mi) là một morph. model_load() nạp chúng cùng model, cùng các
/// đường cong trọng số (kênh `weights`) của animation trong file.
///
/// Trọng số khi vẽ (draw_model(), draw_model_anim(), njin::model3d) là: mặc
/// định của file (`mesh.weights`), rồi animation của `model_pose` đặt lại các
/// morph nó có đường cong, rồi cộng `model_pose::morph_weights`. Morph được trộn
/// trên CPU vào bộ đệm đỉnh của mesh, trước khi xương (nếu có) uốn nó, nên chạy
/// với mọi shader, cả shader của game và bóng đổ. Các mức chi tiết của
/// model_lod_build() giữ morph. draw_instanced3d() và ray3d_model() có bản nhận
/// njin::model_pose để dùng hình đã morph (bản không có nó dùng hình gốc). Model
/// nạp với `model_load_desc::merge` giữ morph của các mesh được gộp.
/// @param ctx Context của engine.
/// @param handle Model.
/// @return Số morph, 0 nếu model không có hay handle không hợp lệ.
i32 model_morph_count(const context &ctx, model_handle handle);

/// Tên của morph thứ `morph`: tên trong `extras.targetNames` của file, hoặc
/// `"<tên mesh>.<số>"` khi file không đặt tên.
/// @param ctx Context của engine.
/// @param handle Model.
/// @param morph 0..model_morph_count() - 1.
/// @return Tên, hoặc chuỗi rỗng nếu handle hay `morph` không hợp lệ.
const char *model_morph_name(const context &ctx, model_handle handle, i32 morph);

/// Tìm morph theo tên (so đúng cả chuỗi).
/// @param ctx Context của engine.
/// @param handle Model.
/// @param name Tên morph.
/// @return Chỉ số 0..model_morph_count() - 1, hoặc -1 nếu không có.
i32 model_morph_find(const context &ctx, model_handle handle, const char *name);

/// Trọng số của mọi morph ở tư thế `pose`, đúng như lần vẽ sẽ dùng (mặc định,
/// animation, rồi `pose.morph_weights`). Để đọc độ mở miệng của một đoạn nói,
/// hay kiểm tra animation.
/// @param ctx Context của engine.
/// @param handle Model.
/// @param pose Tư thế.
/// @param out Mảng nhận trọng số, hoặc nullptr để chỉ đếm.
/// @param count Số phần tử của `out`.
/// @return model_morph_count() (chỉ `count` trọng số đầu được ghi).
i32 model_morph_weights(const context &ctx, model_handle handle, const model_pose &pose, f32 *out, i32 count);

/// Xương `bone` ở tư thế `pose` (cùng cách tính với draw_model_anim(), kể cả
/// khi trộn hai animation), để gắn đồ vào tay, đặt vùng trúng đòn theo xương,
/// hay biết bàn chân đang ở đâu. Nhân với transform của lần vẽ để ra thế giới.
///
/// @code
/// // Điếu thuốc trong tay phải, ở thế giới.
/// const njin::bone_pose3d hand = njin::model_bone_pose(ctx, man, pose, hand_r);
/// const njin::vec3 at = pos + hand.position * scale; // model không xoay
/// @endcode
/// @param ctx Context của engine.
/// @param handle Model có xương.
/// @param pose Tư thế; `anim` -1 (và không trộn) là tư thế gốc.
/// @param bone 0..model_bone_count() - 1.
/// @return Tư thế của xương; giá trị mặc định (gốc tọa độ, các trục chuẩn) nếu
/// handle hay `bone` không hợp lệ.
bone_pose3d model_bone_pose(const context &ctx, model_handle handle, const model_pose &pose, i32 bone);

/// Một tia trong thế giới 3D, dùng để chọn vật bằng chuột, bắn đạn, kiểm tra
/// tầm nhìn.
struct ray3d {
  vec3 origin{0.0f, 0.0f, 0.0f};     ///< Gốc.
  vec3 direction{0.0f, 0.0f, -1.0f}; ///< Hướng, độ dài 1.
};

/// Kết quả khi tia chạm một vật.
struct ray3d_hit {
  bool hit = false;                ///< Tia có chạm không.
  f32 distance = 0.0f;             ///< Khoảng cách từ gốc tia tới điểm chạm.
  vec3 point{0.0f, 0.0f, 0.0f};    ///< Điểm chạm.
  vec3 normal{0.0f, 0.0f, 0.0f};   ///< Pháp tuyến của bề mặt tại điểm chạm.
};

/// Tia từ camera đi qua một điểm trên màn hình, để chọn vật dưới chuột:
/// `camera3d_ray(ctx, cam, mouse_pos(ctx))`. Dùng cùng tỉ lệ khung hình với
/// begin_3d() (screen_size()), nên đúng cả khi có virtual size.
/// @param ctx Context của engine.
/// @param camera Camera đã (hoặc sẽ) dùng cho begin_3d().
/// @param screen Điểm trên màn hình, pixel (như mouse_pos()).
/// @return Tia có gốc ở vị trí camera.
ray3d camera3d_ray(const context &ctx, const camera3d &camera, vec2 screen);

/// Vị trí trên màn hình của một điểm 3D, để đặt nhãn hay thanh máu trên đầu nhân vật.
/// @param ctx Context của engine.
/// @param camera Camera của lần vẽ.
/// @param point Điểm trong thế giới.
/// @param visible Nếu khác nullptr, nhận `false` khi điểm ở sau camera (vị trí
/// trả về khi đó không có nghĩa).
/// @return Vị trí, pixel màn hình (cùng hệ với mouse_pos()).
vec2 camera3d_to_screen(const context &ctx, const camera3d &camera, vec3 point, bool *visible = nullptr);

/// Tia với hộp có các cạnh song song với trục. Gốc tia nằm trong hộp thì chạm ở
/// mặt tia đi ra.
/// @param ray Tia.
/// @param center Tâm hộp.
/// @param size Kích thước theo x, y, z.
/// @return Điểm chạm gần nhất, nếu có.
ray3d_hit ray3d_box(const ray3d &ray, vec3 center, vec3 size);

/// Tia với hình cầu.
/// @param ray Tia.
/// @param center Tâm.
/// @param radius Bán kính.
/// @return Điểm chạm gần nhất, nếu có.
ray3d_hit ray3d_sphere(const ray3d &ray, vec3 center, f32 radius);

/// Tia với mặt phẳng vô hạn qua `point`, pháp tuyến `normal`. Chạm cả hai mặt.
/// @param ray Tia.
/// @param point Một điểm của mặt phẳng.
/// @param normal Pháp tuyến (không cần độ dài 1).
/// @return Điểm chạm, nếu tia không song song và mặt phẳng ở phía trước.
ray3d_hit ray3d_plane(const ray3d &ray, vec3 point, vec3 normal);

/// Tia với một hình SDF, đúng như draw_shape3d() vẽ nó (kể cả khi xoay, bo góc).
/// @param ray Tia.
/// @param shape Hình.
/// @return Điểm chạm gần nhất, nếu có.
ray3d_hit ray3d_shape(const ray3d &ray, const shape3d &shape);

/// Tia với từng tam giác của một model đặt tại `transform`, như draw_model() vẽ nó.
/// Tốn theo số tam giác: dùng cho vài model, không cho cả nghìn.
/// @param ctx Context của engine.
/// @param ray Tia.
/// @param model Model từ model_load().
/// @param transform Vị trí, hướng và tỉ lệ.
/// @return Điểm chạm gần nhất, hoặc không chạm nếu handle không hợp lệ.
ray3d_hit ray3d_model(const context &ctx, const ray3d &ray, model_handle model, const transform3d &transform);

/// Như bản trên, nhưng mesh có morph target được thử ở hình của `pose`: trọng
/// số mặc định, animation và `pose.morph_weights`, đúng như draw_model_anim()
/// vẽ nó (model_morph_weights()). Xương của `pose` không được dùng: tia thử
/// tư thế gốc. Model không có morph thì như bản trên.
/// @param ctx Context của engine.
/// @param ray Tia.
/// @param model Model từ model_load().
/// @param transform Vị trí, hướng và tỉ lệ.
/// @param pose Tư thế cho trọng số morph.
/// @return Điểm chạm gần nhất, hoặc không chạm nếu handle không hợp lệ.
ray3d_hit ray3d_model(const context &ctx, const ray3d &ray, model_handle model, const transform3d &transform,
                      const model_pose &pose);

/// Hình lưới có sẵn để vẽ nhiều bản một lúc bằng draw_instanced3d().
enum mesh3d_kind {
  mesh3d_cube,     ///< Hộp 1 x 1 x 1, tâm ở gốc.
  mesh3d_sphere,   ///< Cầu bán kính 1, tâm ở gốc.
  mesh3d_plane,    ///< Mặt 1 x 1 trên mặt xz, hướng lên `+y`.
  mesh3d_cylinder, ///< Trụ bán kính 1, đáy ở gốc, cao 1 theo `+y`.
  /// Như `mesh3d_sphere` nhưng ít mặt (khoảng 100 tam giác, bản thường khoảng
  /// 3000): cho hàng nghìn vật nhỏ trên màn hình (đầu lính trong một đám đông,
  /// hạt khói), nơi bản mịn chỉ làm GPU vẽ thêm những cạnh không ai thấy.
  mesh3d_sphere_low,
  /// Như `mesh3d_cylinder` nhưng 8 cạnh thay vì 48, cùng lý do.
  mesh3d_cylinder_low,
};

/// Vẽ `count` bản của một hình có sẵn bằng **một** lệnh vẽ, mỗi bản đặt theo dữ
/// liệu của nó trong một bộ đệm instance (instance_buffer_create(),
/// instance_buffer_upload(), như draw_instanced() của 2D). Cho rừng cây, đám đông,
/// mảnh vỡ, hàng nghìn viên gạch.
///
/// Không có `shader`, shader có sẵn của engine đọc mỗi instance như sau, nên vẫn
/// có ánh sáng, bóng đổ, njin::material3d (kể cả `texture`) và njin::fx3d lúc gọi:
/// - `instance0`: vị trí `xyz`, tỉ lệ đều `w` (0 là 1);
/// - `instance1` (từ 8 số mỗi instance): màu `rgba`, nhân với màu của hình;
/// - `instance2` (từ 12 số): góc xoay `xyz`, độ, cùng thứ tự với njin::transform3d;
/// - `instance3` (16 số): tỉ lệ theo x, y, z (0 là 1), nhân với `instance0.w`.
///
/// Có `shader` thì shader đó tự đọc `instance0..3` theo ý nó; engine đặt `mvp`
/// (camera, không có ma trận model) và các uniform ánh sáng như với draw_cube3d().
/// Như mọi hình 3D, chỉ có tác dụng giữa begin_3d() và end_3d(), và vẽ ở end_3d():
/// bộ đệm phải giữ nguyên dữ liệu đến lúc đó.
/// @param ctx Context của engine.
/// @param mesh Hình.
/// @param buffer Bộ đệm đã ghi bằng instance_buffer_upload().
/// @param first Instance đầu tiên.
/// @param count Số instance, bị cắt bớt nếu vượt quá số đã ghi.
/// @param shader Shader của game, hoặc không hợp lệ để dùng shader có sẵn.
void draw_instanced3d(const context &ctx, mesh3d_kind mesh, instance_buffer_handle buffer, u32 first, u32 count,
                      shader_handle shader = {});

/// Vẽ `count` bản của một model bằng một lệnh vẽ cho mỗi phần của model, cùng quy
/// ước dữ liệu instance với bản trên. Mỗi phần giữ ảnh và màu của vật liệu nó
/// (model_material_set()). Normal map chỉ dùng khi game tự đặt
/// `model_material::normal` (normal map trong file bị bỏ qua ở đây); ảnh phát
/// sáng không dùng. Phần có `model_material::shader` vẽ bằng shader đó thay cho
/// `shader`, như draw_model().
/// @param ctx Context của engine.
/// @param model Model từ model_load().
/// @param buffer Bộ đệm đã ghi bằng instance_buffer_upload().
/// @param first Instance đầu tiên.
/// @param count Số instance, bị cắt bớt nếu vượt quá số đã ghi.
/// @param shader Shader của game, hoặc không hợp lệ để dùng shader có sẵn.
void draw_instanced3d(const context &ctx, model_handle model, instance_buffer_handle buffer, u32 first, u32 count,
                      shader_handle shader = {});

/// Như bản trên, với morph target theo `pose` (trọng số mặc định, animation và
/// `pose.morph_weights`, như draw_model_anim()): **mọi** instance của lệnh này có
/// cùng một hình đã morph. Để các instance mang nhiều hình khác nhau (đám đông
/// mỗi người một nét mặt), chia chúng thành vài nhóm, mỗi nhóm một lệnh với
/// `pose` riêng. Xương của `pose` không được dùng. Model không có morph thì như
/// bản trên.
/// @code
/// // 500 người cùng cười: một lệnh vẽ.
/// const njin::f32 smile[] = {1.0f};
/// njin::draw_instanced3d(ctx, face, crowd, 0, 500, njin::model_pose{.morph_weights = smile, .morph_count = 1});
/// @endcode
/// @param ctx Context của engine.
/// @param model Model từ model_load().
/// @param buffer Bộ đệm đã ghi bằng instance_buffer_upload().
/// @param first Instance đầu tiên.
/// @param count Số instance, bị cắt bớt nếu vượt quá số đã ghi.
/// @param pose Tư thế cho trọng số morph.
/// @param shader Shader của game, hoặc không hợp lệ để dùng shader có sẵn.
void draw_instanced3d(const context &ctx, model_handle model, instance_buffer_handle buffer, u32 first, u32 count,
                      const model_pose &pose, shader_handle shader = {});

/// Hiệu ứng cho các hình 3D vẽ sau fx3d_set(): nháy màu và tan biến, như
/// njin::flash_fx và njin::dissolve_fx của sprite.
///
/// Hình 3D vẽ ngay lúc gọi, không phải entity, nên game tự giữ thời gian và đặt
/// mức hiệu ứng mỗi frame. Chỉ shader có sẵn của engine làm hiệu ứng này; hình vẽ
/// bằng shader của game (shader_begin()) không bị ảnh hưởng.
struct fx3d {
  /// Màu tô lên hình, thay màu từng điểm mà vẫn giữ hình dáng (khác `tint`, chỉ
  /// nhân màu). `a` là độ phủ: 0 là không nháy, 1 là tô kín.
  rgba flash{1.0f, 1.0f, 1.0f, 0.0f};
  /// Mức tan biến, 0..1. 0 là còn nguyên, 1 là biến mất hẳn. Hình bị bớt từng
  /// mảng theo một số ngẫu nhiên cố định của mảng.
  f32 dissolve = 0.0f;
  rgba edge_color{1.0f, 0.55f, 0.1f, 1.0f}; ///< Màu viền cháy ở chỗ đang tan. `a` bằng 0 là bỏ viền.
  f32 edge_width = 0.08f; ///< Độ dày viền, theo thang ngẫu nhiên 0..1.
  f32 grain = 0.1f;       ///< Cỡ mỗi mảng, đơn vị thế giới.
  f32 seed = 0.0f;        ///< Đổi hình mẫu tan, để hai vật không tan giống hệt nhau.
  /// Khoét chỗ che (sphere masking), cho camera góc thứ ba: phần hình nằm giữa
  /// camera và `mask_center` (đơn vị thế giới 3D), trong một hình nón loe dần
  /// tới bán kính `mask_radius` ở `mask_center`, bị bỏ đi theo mẫu chấm, mép
  /// mờ dần, nên nhân vật sau tường vẫn thấy được. Mặt đất dưới chân (thấp hơn
  /// `mask_center` quá 0,6 `mask_radius`) giữ nguyên. Bóng đổ không đổi. Đặt khi
  /// vẽ những gì có thể che (nhà, cây), tắt trước khi vẽ nhân vật.
  /// `mask_radius` bằng 0 là tắt.
  vec3 mask_center{0.0f, 0.0f, 0.0f};
  f32 mask_radius = 0.0f; ///< Bán kính chỗ khoét ở `mask_center`, đơn vị thế giới 3D.
};

/// Đặt hiệu ứng cho các hình 3D vẽ sau lệnh này, đến lần gọi tiếp theo hoặc
/// end_3d(). `fx3d_set(ctx, {})` là tắt. Chỉ có tác dụng giữa begin_3d() và end_3d().
///
/// @code
/// // Mục tiêu trúng đạn: nháy trắng 0.1 giây rồi tan trong 0.4 giây.
/// njin::fx3d_set(ctx, {.flash = {1, 1, 1, 1 - t / 0.1f}, .dissolve = t / 0.4f});
/// njin::draw_sphere3d(ctx, pos, 0.6f, njin::colors::red);
/// njin::fx3d_set(ctx, {});
/// @endcode
/// @param ctx Context của engine.
/// @param fx Hiệu ứng.
void fx3d_set(context &ctx, const fx3d &fx);

/// Kiểu vẽ có sẵn của hạt 3D (particles3d_desc::style): shader của engine tô
/// từng hạt, theo màu và độ trong suốt của nó (giảm theo tuổi), thời gian và
/// mặt trời của light3d_set().
enum particle3d_style {
  particle3d_flat,  ///< Như emitter tả: ảnh hay hình của nó nhân màu (mặc định).
  /// Khói, bụi, hơi: quả cầu mềm, mép bị noise trôi chậm trong không gian ăn
  /// vào nên một dòng hạt thành một đám mây cuộn; mặt phía mặt trời sáng, mặt
  /// kia tối theo ánh sáng nền. Hạt càng già càng mỏng, rách. Dùng `blend_alpha`.
  particle3d_smoke,
  /// Lửa: lõi trắng nóng qua vàng, cam tới viền đỏ, lập loè theo noise; nguội
  /// dần theo tuổi. Dùng `blend_additive`.
  particle3d_fire,
  /// Tia lửa: vệt có sợi lõi trắng nóng trong quầng cam, sáng ở đầu, tắt dần
  /// về đuôi, đỏ dần khi nguội. Dùng với `stretch` và `blend_additive`.
  particle3d_spark,
  /// Luồng lửa phụt của động cơ phản lực, tên lửa: phát dày các hạt nhanh, cùng
  /// tốc độ, có `stretch`, `blend_additive`, màu bắt đầu có `a` bằng 1 và kết thúc
  /// bằng 0, để tuổi của hạt là quãng cách miệng phun. Gần miệng phun lõi trắng
  /// xanh, xa dần vàng, cam, đỏ, có các vạch sáng (shock diamond) đứng yên trong
  /// dòng khí. Kênh đỏ của màu là lực đẩy, 0..1.
  particle3d_flame,
};

/// Cách đặt một loạt hạt vào thế giới 3D, dùng với particles3d_spawn().
struct particles3d_desc {
  /// Hướng phát. Emitter có `spread` dưới 360 độ phát trong hình nón quanh hướng
  /// này, nửa góc mở bằng `spread / 2`; `angle` của emitter bị bỏ qua.
  vec3 direction{0.0f, 1.0f, 0.0f};
  /// Đổi đơn vị của emitter (pixel, như các mẫu njin::fx) sang đơn vị thế giới 3D:
  /// nhân vào tốc độ, kích thước, trọng lực và `area`. Trọng lực `{0, 400}`
  /// (rơi xuống màn hình) thành rơi theo `-y`.
  f32 scale = 0.02f;
  /// Kéo dài mỗi hạt theo hướng nó bay: dài thêm quãng nó bay trong `stretch`
  /// giây, rộng bằng kích thước hạt, nên tia lửa, mưa, mảnh vụn thành vệt.
  /// Bỏ qua xoay của hạt. 0 là hạt vuông quay mặt về camera như thường.
  f32 stretch = 0.0f;
  /// Shader của game vẽ các hạt này, thay cho shader có sẵn: khói cuộn bằng
  /// noise, lõi lửa nóng sáng. Mỗi hạt là một hình chữ nhật, đỉnh có toạ độ thế
  /// giới (`vertexPosition`; `mvp` đã gồm camera), toạ độ ảnh 0..1
  /// (`vertexTexCoord`), màu của hạt lúc đó (`vertexColor`, độ trong suốt giảm
  /// theo tuổi), ảnh của emitter ở `texture0`. Đặt uniform bằng `shader_set_*`
  /// trước end_3d(). Không hợp lệ là shader có sẵn (`style`).
  shader_handle shader{};
  /// Kiểu vẽ có sẵn, khi không có `shader`: khói, lửa, tia lửa, luồng lửa phụt.
  particle3d_style style = particle3d_flat;
};

/// Phát một loạt `count` hạt tại `pos`, dùng lại một emitter 2D: các mẫu njin::fx
/// (explosion(), sparks(), dust()...) hay emitter tự cấu hình.
///
/// Hạt luôn quay mặt về camera, được mô phỏng mỗi frame theo delta() (nên dừng
/// trong hitstop) và vẽ ở end_3d(), sau các hình khác của lần vẽ đó: tường vẫn che
/// được hạt, còn hạt không che nhau. Dùng hình tròn, hình vuông hay texture, màu,
/// kích thước, trọng lực, lực cản, xoay và cách trộn màu của emitter. Chỉ phát
/// theo loạt; muốn phát liên tục (khói ống khói) thì dùng particles3d_emit().
/// @param ctx Context của engine.
/// @param emitter Cấu hình hạt.
/// @param pos Vị trí phát.
/// @param count Số hạt.
/// @param desc Hướng phát và tỉ lệ đơn vị.
void particles3d_spawn(context &ctx, const particle_emitter &emitter, vec3 pos, i32 count,
                       const particles3d_desc &desc = {});

/// Dạng của particle3d_area.
enum particle3d_area_kind {
  particle3d_area_point, ///< Một điểm, `center`.
  particle3d_area_ring,  ///< Đường tròn bán kính `radius` quanh `center` (vòng lửa, vòng phép thuật).
  particle3d_area_disc,  ///< Cả mặt tròn bán kính `radius`, rải đều (đống lửa, mặt nước).
};

/// Vùng sinh hạt của particles3d_emit(): một điểm, một vòng hay một đĩa nằm
/// vuông góc với `normal`. Cộng thêm `area` của emitter như particles3d_spawn().
struct particle3d_area {
  particle3d_area_kind kind = particle3d_area_point; ///< Dạng vùng.
  vec3 center{};                    ///< Tâm, đơn vị thế giới 3D.
  vec3 normal{0.0f, 1.0f, 0.0f};    ///< Pháp tuyến của mặt phẳng chứa vòng hay đĩa.
  f32 radius = 0.0f;                ///< Bán kính, đơn vị thế giới 3D.
  /// Nghiêng hướng phát ra ngoài theo bán kính, ở vòng và đĩa: mỗi hạt phát
  /// quanh `particles3d_desc::direction` cộng `outward` lần hướng từ tâm ra chỗ nó
  /// sinh. 0 là không nghiêng; 1 là chéo 45 độ, như nước toả ra từ đài phun.
  f32 outward = 0.0f;
};

/// Số hạt cần sinh ở frame này để phát đều `emitter.rate` hạt mỗi giây.
///
/// Phần lẻ được giữ trong `emitter.emit_accum` sang frame sau, nên tốc độ thấp
/// (vài hạt mỗi giây) vẫn đều, và `rate` đổi được mỗi frame (khói xe dày theo ga).
/// Trả 0 khi `emitting` là false hay `rate` bằng 0. Đã tính
/// njin::graphics_quality::particles và theo delta() (nên dừng trong hitstop).
///
/// Dùng khi game tự chọn chỗ sinh từng hạt (một điểm ngẫu nhiên trên bề mặt
/// model tìm bằng tia, một trong vài điểm cho trước) rồi gọi particles3d_spawn()
/// cho mỗi hạt. Vùng là điểm, vòng hay đĩa thì particles3d_emit() làm cả hai việc.
/// @code
/// // Khói từ một trong các ống khói, dày theo số người trong nhà.
/// smoke.rate = 2.0f + 0.5f * people;
/// for (njin::i32 n = njin::particles3d_due(ctx, smoke); n > 0; n--)
///   njin::particles3d_spawn(ctx, smoke, chimneys[rng.range(0, chimney_count - 1)], 1, {.scale = 1.0f});
/// @endcode
/// @param ctx Context của engine.
/// @param emitter Emitter do game giữ giữa các frame; dùng `rate`, `emitting` và `emit_accum`.
/// @return Số hạt.
i32 particles3d_due(context &ctx, particle_emitter &emitter);

/// Phát liên tục: mỗi frame sinh các hạt đến hạn theo `emitter.rate` (như
/// particles3d_due()), mỗi hạt ở một chỗ ngẫu nhiên trong `area`, tất cả trong
/// một loạt. Gọi mỗi frame; ngừng gọi (hoặc `emitting = false`) là ngừng phát,
/// hạt đã bay vẫn sống hết tuổi.
///
/// Hạt 3D không phải entity, nên game giữ `emitter` (một bản sao chép, không phải
/// hằng số) như giữ trạng thái: engine ghi phần lẻ vào `emit_accum`. Mọi trường
/// đổi được giữa hai lần gọi, kể cả `rate` và `area`.
/// @code
/// // Một vòng lửa quanh cổng dịch chuyển: 300 hạt mỗi giây, toả ra ngoài một chút.
/// njin::particle_emitter ring = njin::fx::fire(); // giữ trong state của game
/// ring.rate = 300.0f;
/// njin::particles3d_emit(ctx, ring,
///                        {.kind = njin::particle3d_area_ring, .center = portal, .radius = 1.5f, .outward = 0.3f},
///                        {.style = njin::particle3d_fire});
/// @endcode
/// @param ctx Context của engine.
/// @param emitter Cấu hình hạt và trạng thái phát.
/// @param area Vùng sinh.
/// @param desc Hướng phát, tỉ lệ đơn vị và kiểu vẽ, như particles3d_spawn().
void particles3d_emit(context &ctx, particle_emitter &emitter, const particle3d_area &area,
                      const particles3d_desc &desc = {});

/// Một chỗ ngẫu nhiên trong `area`, rải như particles3d_emit() rải hạt (đều trên
/// vòng, đều trên mặt đĩa). Cho các chùm hạt sinh cùng một chỗ: tia lửa bắn ra
/// từng nhúm khi củi nổ lách tách.
/// @param ctx Context của engine (lấy random()).
/// @param area Vùng.
/// @return Vị trí, đơn vị thế giới 3D.
vec3 particles3d_area_point(context &ctx, const particle3d_area &area);

/// Xóa mọi hạt 3D đang bay, ví dụ khi đổi màn.
/// @param ctx Context của engine.
void particles3d_clear(context &ctx);

/// Số hạt 3D đang sống.
/// @param ctx Context của engine.
/// @return Số hạt.
i32 particles3d_count(const context &ctx);

/// Component: một model vẽ tại njin::transform3d của entity, trong mọi lần vẽ 3D
/// có `camera3d::entities` (engine thêm nó ở end_3d(), cùng các lệnh vẽ của game).
///
/// Engine tăng `pose.time` và `pose.blend_time` mỗi frame (theo delta(), nhân
/// `speed`), nên game chỉ cần chọn animation:
///
/// @code
/// const auto e = reg.create();
/// reg.emplace<njin::transform3d>(e, njin::transform3d{.position = {0, 0, -4}});
/// reg.emplace<njin::model3d>(e, njin::model3d{.model = robot, .pose = {.anim = walk}});
/// @endcode
struct model3d {
  model_handle model{};          ///< Model từ model_load().
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nhân vào màu của model.
  model_pose pose{};             ///< Tư thế, như draw_model_anim().
  f32 speed = 1.0f;              ///< Tốc độ chạy animation. 0 là dừng.
  fx3d fx{};                     ///< Nháy màu và tan biến, như fx3d_set().
  bool visible = true;           ///< Ẩn mà không cần gỡ component.
};

/// Component: một hình SDF (như draw_shape3d()) vẽ tại njin::transform3d của
/// entity, trong mọi lần vẽ 3D có `camera3d::entities`. Vị trí và góc xoay lấy
/// từ transform (`shape.position` và `shape.rotation` bị bỏ qua), nên một entity
/// có thêm njin::body3d được vẽ đúng chỗ vật lý đặt nó.
struct shape3d_render {
  shape3d shape{};                    ///< Loại hình và kích thước.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu.
  material3d material{};              ///< Bề mặt, như material3d_set().
  fx3d fx{};                          ///< Nháy màu và tan biến, như fx3d_set().
  bool visible = true;                ///< Ẩn mà không cần gỡ component.
};
/// @}
} // namespace njin
