#pragma once
// Thuộc feature 3d: game cần bật nó bằng njin_use(<game> FEATURES 3d)
// (cmake/njin.cmake), hoặc link njin::rt.
#if !defined(NJIN_ENGINE_INTERNAL) && !defined(NJIN_FEATURE_3D)
#error "njin_world3d.h is part of the njin feature '3d', which this game does not use: add it with njin_use(<game> FEATURES 3d)"
#endif
#include "_math.h"
#include "njin_3d.h"
#include "njin_procgen.h"

namespace njin {
struct context;

/// @addtogroup grp_world3d
/// @{

// ---------------------------------------------------------------------------
// Địa hình
// ---------------------------------------------------------------------------

/// Số lớp bề mặt tối đa của một địa hình (cỏ, đất, đá, tuyết...).
inline constexpr i32 terrain3d_layer_max = 4;

/// Một lớp bề mặt của địa hình: ảnh lặp lại trên mặt đất, và quy tắc nó phủ
/// chỗ nào khi njin::terrain3d_desc bật `auto_splat`.
///
/// Ảnh dán theo vị trí trong thế giới (x, z), mỗi ô ảnh `tile` mét, và được trộn
/// với chính nó ở một tỉ lệ khác nên không lộ thành lưới. Chỗ dốc đứng (vách đá)
/// ảnh được chiếu từ ba phía, không bị kéo dài.
struct terrain3d_layer {
  texture_handle albedo{}; ///< Ảnh màu. Không hợp lệ là chỉ dùng `color`.
  texture_handle normal{}; ///< Normal map (xanh lá hướng lên như glTF). Không hợp lệ là không dùng.
  /// Ảnh độ nhám, đọc kênh đỏ: trắng là nhám mờ, đen là nhẵn bóng, xám giữa (0.5)
  /// là đúng `terrain3d_desc::specular` và `shininess`; nhẵn hơn thì bóng gắt và
  /// sáng hơn, nhám hơn thì nhạt dần tới tắt. Ánh sáng PBR dùng thẳng làm độ nhám.
  /// Chiếu như `normal` (engine gộp hai ảnh làm một, không tốn thêm chỗ gắn
  /// texture). Không hợp lệ là cả lớp theo `specular` và `shininess`.
  texture_handle roughness{};
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nhân vào ảnh (hoặc màu trơn khi không có ảnh).
  f32 tile = 4.0f; ///< Cạnh một ô ảnh, mét.
  /// Phá lặp, 0..1 (0 là tắt): ảnh nhìn từ trên xuống được lấy hai bản dời đi những
  /// khoảng đổi dần theo mặt đất rồi trộn vào nhau, nên các ô không còn thẳng hàng
  /// thành lưới khi nhìn xa. Hợp ảnh mềm không có nét rõ (cỏ, cát, đất); ảnh có
  /// đường nét (đá xếp, gạch) chỗ trộn sẽ lộ hai lớp nét. Đọc ảnh gấp đôi.
  f32 detile = 0.0f;
  /// Khi phá lặp, mỗi vùng còn lấy ảnh ở cỡ riêng, to nhỏ ngẫu nhiên tới tỉ lệ này
  /// (0..0.9, 0 là cùng cỡ): chi tiết của ảnh lúc to lúc nhỏ trên mặt đất thay vì
  /// đều tăm tắp. Chỉ có tác dụng khi `detile` > 0.
  f32 detile_scale = 0.0f;
  /// Lấp lánh dưới nắng, 0..4 (0 là tắt): những tinh thể nhỏ cỡ vài cm lóe sáng khi
  /// mặt nghiêng của chúng phản chiếu mặt trời vào mắt, chỉ nơi có nắng (không trong
  /// bóng), và nhấp nháy khi camera di chuyển. Cho tuyết, cát, băng. Mờ dần tới 60 m.
  f32 glitter = 0.0f;
  /// Ảnh độ cao cho hình nổi thật (`relief`), đọc kênh đỏ: trắng là nhô ra, đen là
  /// lõm vào, xám giữa (0.5) là đúng mặt đất. Dán y như `albedo` (cùng `tile`, chiếu
  /// từ ba phía ở vách dốc), nên vẽ nó từ chính ảnh màu: vết nứt trong ảnh màu là
  /// rãnh tối, mỗi phiến đá một mức. Không hợp lệ là không có hình nổi.
  texture_handle height{};
  /// Hình nổi thật, mét (0 là tắt): độ chênh giữa chỗ đen và chỗ trắng của `height`.
  /// Mặt đất gần camera (njin::terrain3d_desc::relief_distance) được chia lưới mịn
  /// hơn (njin::terrain3d_desc::relief_detail) và đẩy ra, vào theo pháp tuyến bấy nhiêu, nhân trọng số của lớp, nên
  /// vách đá có khe nứt và gờ phiến thật (bóng đổ, viền) đúng chỗ ảnh màu vẽ chúng,
  /// và mờ dần về mặt trơn ở mép lớp và ở xa. Chỉ đổi hình vẽ ra: terrain3d_height(),
  /// va chạm và cỏ vẫn theo mặt đất trơn. Không hợp với `detile` (ảnh màu bị dời đi,
  /// hình nổi thì không).
  f32 relief = 0.0f;
  /// @name Quy tắc tự phủ (njin::terrain3d_desc::auto_splat)
  /// Lớp phủ chỗ có độ cao trong `[min_height, max_height]` và độ dốc trong
  /// `[min_slope, max_slope]`, mép mờ dần trong `blend`. Lớp sau đè lên lớp trước,
  /// nên đặt lớp phủ rộng nhất (cỏ) đầu tiên.
  /// @{
  f32 min_height = -1.0e9f; ///< Độ cao thấp nhất, mét (cùng hệ với terrain3d_height()).
  f32 max_height = 1.0e9f;  ///< Độ cao cao nhất, mét.
  f32 min_slope = 0.0f;     ///< Độ dốc nhỏ nhất, độ (0 là bằng phẳng).
  f32 max_slope = 90.0f;    ///< Độ dốc lớn nhất, độ.
  f32 blend = 3.0f;         ///< Độ rộng vùng chuyển: mét cho độ cao, độ cho độ dốc.
  /// @}
};

/// Cách dựng một địa hình bằng terrain3d_create().
///
/// Địa hình là một lưới độ cao vuông `resolution` x `resolution` mẫu, phủ một hình
/// vuông cạnh `size` mét có góc (x nhỏ nhất, z nhỏ nhất) ở `origin`. Độ cao lấy từ
/// `heights`, hoặc nếu không có thì từ `heightmap`, hoặc nếu không có nữa thì từ
/// `noise`.
///
/// Engine chia lưới thành các mảnh `chunk_quads` x `chunk_quads` ô, bỏ mảnh ngoài tầm
/// nhìn, và vẽ mảnh ở xa với ít chi tiết hơn (mỗi lần xa gấp đôi `lod_distance` thì
/// bớt một nửa số ô mỗi cạnh). Mép mảnh có "váy" thả xuống nên hai mảnh khác mức chi
/// tiết không hở khe.
struct terrain3d_desc {
  vec3 origin{0.0f, 0.0f, 0.0f}; ///< Góc x nhỏ nhất, z nhỏ nhất; `y` cộng vào mọi độ cao.
  f32 size = 256.0f;             ///< Cạnh hình vuông, mét.
  /// Số mẫu độ cao mỗi cạnh (ít nhất 3). Khoảng cách hai mẫu là `size / (resolution - 1)`.
  i32 resolution = 257;
  /// Độ cao từng mẫu, mét (cộng `origin.y`), `resolution * resolution` số theo hàng
  /// (hàng `z`, rồi cột `x`). Được chép lại. nullptr là dùng nguồn sau.
  const f32 *heights = nullptr;
  /// Ảnh độ cao: ảnh xám (kênh đỏ, 0 đến 1) hoặc file `.r16`/`.raw` (16 bit, vuông,
  /// little-endian). Được co giãn về `resolution`. Ảnh 8 bit chỉ có 256 bậc: đặt
  /// `smooth` để mượt bậc. nullptr là dùng `noise`.
  const char *heightmap = nullptr;
  /// Nhiễu sinh độ cao khi không có hai nguồn trên, tính theo mét của thế giới
  /// (`frequency` là số gò mỗi mét). Kết quả được kéo giãn cho thấp nhất là 0 và cao
  /// nhất là 1 trước khi nhân `height_scale`.
  noise_desc noise{.seed = 1, .frequency = 0.004f, .fractal = fractal_fbm, .octaves = 5};
  f32 height_scale = 40.0f; ///< Giá trị 1 của ảnh hay nhiễu thành bao nhiêu mét.
  i32 smooth = 0;           ///< Số lượt làm mượt độ cao (trung bình 3 x 3) sau khi đọc.
  terrain3d_layer layers[terrain3d_layer_max]{}; ///< Các lớp bề mặt.
  i32 layer_count = 1;      ///< Số lớp dùng, 1 đến njin::terrain3d_layer_max.
  /// Tự tính lớp nào phủ chỗ nào theo quy tắc của mỗi lớp. Tắt thì dùng `splatmap`
  /// (hoặc chỉ lớp 0), và vẽ thêm bằng terrain3d_paint().
  bool auto_splat = true;
  /// Ảnh trọng số các lớp khi tắt `auto_splat`: kênh đỏ, lục, lam, alpha là lớp 0
  /// đến 3. Được co giãn về `resolution`. nullptr là chỉ lớp 0.
  const char *splatmap = nullptr;
  i32 chunk_quads = 32;      ///< Số ô mỗi cạnh của một mảnh: 8, 16, 32, 64 hoặc 128.
  f32 lod_distance = 60.0f;  ///< Mảnh gần hơn khoảng này (mét) vẽ đủ chi tiết.
  i32 lod_levels = 4;        ///< Số mức chi tiết, 1 đến 6.
  f32 specular = 0.05f;      ///< Độ bóng của mặt đất (ướt thì tăng thêm, xem njin::weather3d).
  f32 shininess = 16.0f;     ///< Độ nhọn điểm sáng bóng.
  /// Tuyết phủ, 0..1 (0 là không): một lớp trắng trên chỗ bằng và thoải, theo những
  /// mảng to nhỏ (nhiễu trong thế giới) khép dần lại khi tuyết dày, 1 là kín mặt đất
  /// bằng; chỗ dốc (vách đá) và vết lún (terrain3d_imprint_stamp()) lộ đất. Lấp lánh
  /// dưới nắng, không ướt. Chỉ đổi màu, không thêm hình. Đổi lúc chạy bằng
  /// terrain3d_snow_set().
  f32 snow = 0.0f;
  rgba snow_color{0.95f, 0.96f, 1.0f, 1.0f}; ///< Màu tuyết của `snow`.
  /// Tầm của hình nổi (njin::terrain3d_layer::relief), mét: các mảnh gần camera
  /// hơn khoảng này mà có lớp có hình nổi thì vẽ bằng lưới mịn, hình nổi mờ dần từ
  /// 70% tầm. Xa hơn là mặt trơn như cũ. Lớn thì tốn thêm đỉnh (mỗi mảnh 8 x 8 ô của
  /// lưới độ cao là khoảng 16 nghìn đỉnh).
  f32 relief_distance = 40.0f;
  /// Số đỉnh mỗi cạnh ô của lưới độ cao ở chỗ vẽ nổi, 1 đến 8: ít là mặt to, mềm,
  /// kiểu low-poly; nhiều là theo sát nét nhỏ của ảnh độ cao (và tốn đỉnh hơn).
  i32 relief_detail = 4;
  bool cast_shadows = true;  ///< Đổ bóng (đồi che nắng). Luôn nhận bóng.
  bool collision = true;     ///< Tạo body va chạm (height field của Jolt) cho nhân vật, xe và vật.
  f32 friction = 0.8f;       ///< Ma sát của body va chạm.
  u64 user = 0;              ///< Giá trị body3d_user() của body va chạm.
};

/// Tạo địa hình. Gọi được ở mọi phase (không cần begin_3d()).
///
/// Ảnh của các lớp được bật mipmap (texture_set_filter() với njin::filter_mipmap) để
/// mặt đất ở xa không nhấp nháy.
/// @param ctx Context của engine.
/// @param desc Cách dựng.
/// @return Handle, hoặc handle không hợp lệ nếu `desc` sai (cảnh báo nói vì sao).
terrain3d_handle terrain3d_create(context &ctx, const terrain3d_desc &desc);

/// Hủy địa hình cùng body va chạm của nó. Cỏ và vật rải trên nó không còn vẽ.
/// @param ctx Context của engine.
/// @param handle Địa hình. Handle không hợp lệ bị bỏ qua.
void terrain3d_destroy(context &ctx, terrain3d_handle handle);

/// Vẽ địa hình trong lần vẽ 3D đang mở (giữa begin_3d() và end_3d()). Nhận ánh sáng,
/// bóng đổ, đèn và sương mù như mọi hình 3D.
/// @param ctx Context của engine.
/// @param handle Địa hình.
void draw_terrain3d(const context &ctx, terrain3d_handle handle);

/// Độ cao mặt đất tại `(x, z)`, đúng như lưới vẽ ở mức chi tiết cao nhất và như
/// body va chạm (cùng cách chia ô thành tam giác). Ngoài địa hình thì lấy mép gần nhất.
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param x Toạ độ x, mét.
/// @param z Toạ độ z, mét.
/// @return Độ cao, mét; 0 nếu handle không hợp lệ.
f32 terrain3d_height(const context &ctx, terrain3d_handle handle, f32 x, f32 z);

/// Pháp tuyến mặt đất tại `(x, z)` (độ dài 1, hướng lên).
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param x Toạ độ x, mét.
/// @param z Toạ độ z, mét.
/// @return Pháp tuyến; `{0, 1, 0}` nếu handle không hợp lệ.
vec3 terrain3d_normal(const context &ctx, terrain3d_handle handle, f32 x, f32 z);

/// `(x, z)` có nằm trên địa hình không.
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param x Toạ độ x, mét.
/// @param z Toạ độ z, mét.
/// @return `true` nếu nằm trong hình vuông của nó.
bool terrain3d_contains(const context &ctx, terrain3d_handle handle, f32 x, f32 z);

/// Trọng số của lớp `layer` tại `(x, z)`, 0 đến 1 (các lớp cộng lại bằng 1). Để biết
/// nhân vật đang đứng trên gì (tiếng bước chân cỏ hay đá).
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param x Toạ độ x, mét.
/// @param z Toạ độ z, mét.
/// @param layer 0..njin::terrain3d_layer_max - 1.
/// @return Trọng số; 0 nếu handle hay `layer` không hợp lệ.
f32 terrain3d_layer_weight(const context &ctx, terrain3d_handle handle, f32 x, f32 z, i32 layer);

/// Body va chạm của địa hình (tĩnh), để nhận ra nó trong raycast và va chạm.
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @return Body, hoặc handle không hợp lệ nếu tắt `collision`.
body3d_handle terrain3d_body(const context &ctx, terrain3d_handle handle);

/// Kiểu sửa của njin::terrain3d_brush.
enum terrain3d_brush_kind {
  terrain3d_raise,   ///< Nâng lên `strength` mét ở tâm.
  terrain3d_lower,   ///< Hạ xuống `strength` mét ở tâm.
  terrain3d_flatten, ///< Kéo về độ cao `height`, một phần `strength` (0..1) mỗi lần.
  terrain3d_smooth,  ///< Làm mượt, một phần `strength` (0..1) mỗi lần.
  /// Đào lòng chảo, chỉ hạ, không bao giờ nâng: đáy phẳng ở độ cao `height` trong
  /// phần trong của bán kính (bỏ phần `falloff`), thành dốc đều lên `strength` mét
  /// mỗi mét từ đó ra tới mép; chỗ nào đã thấp hơn thì giữ nguyên. Nhiều nét chồng
  /// nhau (theo một đường, hay rải trong một vùng) thành lòng sông, lòng hồ có bờ
  /// dốc đều bất kể đào sâu bao nhiêu. Bán kính phải đủ để thành dốc lên tới mặt
  /// đất, không thì mép nét thành vách.
  terrain3d_carve,
  /// Đắp nền, chỉ nâng, không bao giờ hạ: ngược với `terrain3d_carve`. Mặt phẳng ở
  /// độ cao `height` trong phần trong của bán kính (bỏ phần `falloff`), sườn dốc đều
  /// xuống `strength` mét mỗi mét từ đó ra tới mép; chỗ nào đã cao hơn thì giữ
  /// nguyên. Cho nền đắp, mô đất, bờ đê có mặt bằng và sườn đều. Bán kính phải đủ để
  /// sườn xuống tới mặt đất, không thì mép nét thành vách.
  terrain3d_fill,
};

/// Một nét sửa địa hình hình tròn, mạnh nhất ở tâm và nhạt dần ra mép.
struct terrain3d_brush {
  terrain3d_brush_kind kind = terrain3d_raise; ///< Kiểu sửa.
  vec3 center{0.0f, 0.0f, 0.0f}; ///< Tâm (dùng `x` và `z`).
  f32 radius = 4.0f;             ///< Bán kính, mét.
  /// Mét (nâng, hạ), tỉ lệ 0..1 (san phẳng, làm mượt), hoặc độ dốc của thành, mét
  /// mỗi mét (đào, đắp nền).
  f32 strength = 0.5f;
  f32 height = 0.0f;             ///< Độ cao đích của `terrain3d_flatten`, đáy của `terrain3d_carve`, mặt của `terrain3d_fill`, mét.
  /// Phần ngoài của bán kính mờ dần, 0 (cứng) đến 1 (mờ từ tâm).
  f32 falloff = 0.6f;
};

/// Sửa độ cao địa hình bằng một nét: lưới vẽ, body va chạm, lớp tự phủ, cỏ và vật
/// rải trên vùng đó đều theo ngay (vật rải giữ chỗ, chỉ đổi độ cao).
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param brush Nét sửa.
void terrain3d_edit(context &ctx, terrain3d_handle handle, const terrain3d_brush &brush);

/// Vẽ thêm lớp `layer` lên địa hình quanh `center` (lớp khác nhạt đi). Nét vẽ được
/// giữ khi sửa độ cao, kể cả khi `auto_splat` bật.
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param center Tâm (dùng `x` và `z`).
/// @param radius Bán kính, mét.
/// @param layer Lớp.
/// @param strength Mỗi lần thêm bao nhiêu ở tâm, 0..1.
void terrain3d_paint(context &ctx, terrain3d_handle handle, vec3 center, f32 radius, i32 layer, f32 strength);

/// Đổi tuyết phủ địa hình (njin::terrain3d_desc::snow): tuyết rơi dày dần, tan theo
/// mùa. Có hiệu lực ở lần vẽ sau, không tốn gì: đổi được mỗi frame.
/// @param ctx Context của engine.
/// @param handle Địa hình. Handle không hợp lệ bị bỏ qua.
/// @param snow Tuyết phủ, 0..1.
/// @param color Màu tuyết.
void terrain3d_snow_set(context &ctx, terrain3d_handle handle, f32 snow, rgba color = {0.95f, 0.96f, 1.0f, 1.0f});

/// Vết lún tạm trên địa hình: dấu chân, vệt bánh xe, rãnh xe trượt trong tuyết
/// hay cát, mờ dần rồi mất. Giữ trong một ô vuông quanh một điểm (thường là
/// camera, xem terrain3d_imprint_follow()), mỗi texel một độ lún và một gờ đất
/// đùn lên bên cạnh. Mặt đất không đổi hình: shader của địa hình nghiêng pháp
/// tuyến theo độ lún và gờ, và nhuộm chỗ lún, nên vết có bóng và mép sáng như
/// có chiều sâu mà không tốn lưới hay va chạm.
struct terrain3d_imprint_desc {
  f32 texel = 0.05f;   ///< Mét mỗi texel.
  i32 size = 1024;     ///< Số texel mỗi cạnh của ô giữ vết (64..4096). 0 là tắt.
  f32 depth = 0.1f;    ///< Một vết lún hết trông sâu bao nhiêu, mét.
  f32 rim = 0.04f;     ///< Một gờ đầy trông cao bao nhiêu, mét.
  f32 lifetime = 5.0f; ///< Một vết còn bao lâu, giây (thời gian của game).
  f32 fade = 2.0f;     ///< Mờ dần trong bấy nhiêu giây cuối của `lifetime`.
  /// Màu nhân vào chỗ lún hết (tuyết nén ánh xanh, cát ướt sẫm); `a` là độ đậm.
  rgba tint{1.0f, 1.0f, 1.0f, 0.0f};
};

/// Bật, đổi hay tắt (`size` = 0) vết lún của địa hình. Bật lại thì xóa vết cũ.
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param desc Cách giữ và vẽ vết.
void terrain3d_imprint_set(context &ctx, terrain3d_handle handle, const terrain3d_imprint_desc &desc);

/// Dời ô giữ vết cho tâm của nó ở `center` (dùng `x` và `z`); gọi mỗi frame với
/// vị trí camera. Phần ra khỏi ô bị quên; vết hết hạn được xóa ở đây.
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param center Tâm mới.
void terrain3d_imprint_follow(context &ctx, terrain3d_handle handle, vec3 center);

/// Một hình vết cho terrain3d_imprint_stamp(): `width * height` cặp byte, hàng
/// đầu là mũi của vết (hướng `yaw`), mỗi cặp là độ lún rồi độ gờ, 0..255.
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param pressed_rim Các cặp byte.
/// @param width Số cột.
/// @param height Số hàng.
/// @return Số của hình, hoặc -1 nếu dữ liệu sai.
i32 terrain3d_imprint_shape(context &ctx, terrain3d_handle handle, const u8 *pressed_rim, i32 width, i32 height);

/// Một vết in xuống.
struct terrain3d_stamp {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Tâm (dùng `x` và `z`).
  f32 yaw = 0.0f;                  ///< Mũi của hình chỉ theo hướng này, độ (0 là +z, 90 là +x).
  vec2 half_size{0.1f, 0.1f};      ///< Nửa bề ngang và nửa bề dài của hình, mét.
  i32 shape = -1;                  ///< Từ terrain3d_imprint_shape(); -1 là vết tròn mềm mép.
  f32 strength = 1.0f;             ///< Nhân vào độ lún và gờ của hình, 0..1.
};

/// In một vết: mỗi texel giữ chỗ lún và gờ sâu hơn trong hai vết cũ và mới, và
/// tính tuổi lại từ bây giờ. Vết ngoài ô giữ vết bị bỏ.
/// @param ctx Context của engine.
/// @param handle Địa hình.
/// @param stamp Vết.
void terrain3d_imprint_stamp(context &ctx, terrain3d_handle handle, const terrain3d_stamp &stamp);

/// Xóa mọi vết (bắt đầu lại một màn).
/// @param ctx Context của engine.
/// @param handle Địa hình.
void terrain3d_imprint_clear(context &ctx, terrain3d_handle handle);

// ---------------------------------------------------------------------------
// Cỏ và vật rải
// ---------------------------------------------------------------------------

/// Cách mọc của một thảm cỏ trên địa hình (grass3d_create()).
///
/// Mỗi ngọn cỏ là vài tam giác, vẽ hàng chục nghìn ngọn bằng instancing. Cỏ chỉ được
/// tạo cho vùng quanh camera (trong `draw_distance`), thưa dần và thấp dần về xa rồi
/// biến mất, nên địa hình rộng mấy cũng chỉ tốn cho phần gần. Cỏ đung đưa theo gió
/// (wind3d_set(), hoặc gió của njin::weather3d khi vẽ trời bằng draw_sky3d()).
struct grass3d_desc {
  terrain3d_handle terrain{}; ///< Địa hình cỏ mọc trên.
  f32 density = 6.0f;         ///< Số ngọn mỗi mét vuông nơi mọc dày nhất.
  f32 height = 0.45f;         ///< Chiều cao ngọn, mét.
  f32 height_jitter = 0.4f;   ///< Ngọn cao thấp khác nhau tới tỉ lệ này của `height`.
  f32 width = 0.05f;          ///< Bề rộng gốc ngọn, mét.
  /// Ngọn rộng theo chiều cao của nó: `width` là bề rộng của ngọn cao đúng `height`,
  /// ngọn thấp hơn (lệch, vùng thấp, mép thảm) hẹp đi theo cùng tỉ lệ.
  bool width_by_height = false;
  rgba base_color{0.12f, 0.30f, 0.07f, 1.0f}; ///< Màu ở gốc.
  rgba tip_color{0.52f, 0.70f, 0.28f, 1.0f};  ///< Màu ở ngọn.
  f32 color_jitter = 0.15f;   ///< Mỗi ngọn sáng tối khác nhau tới tỉ lệ này.
  /// Lớp địa hình cỏ mọc trên: số ngọn nhân với trọng số của lớp đó
  /// (terrain3d_layer_weight()). -1 là mọc mọi nơi.
  i32 layer = 0;
  f32 max_slope = 35.0f;      ///< Không mọc chỗ dốc hơn, độ.
  /// Không mọc chỗ thấp hơn mức này, mét (vd. ngay trên mặt nước); thưa và thấp dần
  /// trong 0.4 m trên nó.
  f32 min_height = -1.0e9f;
  /// Mép thảm cỏ (lớp nhạt dần, gần `max_slope`, gần `min_height`), 0..1 (0 là tắt):
  /// ngọn thấp dần về phía mép, ở mép thấp còn `1 - edge_shrink` lần, và mọc dày hơn
  /// trọng số ở đó, nên mép là một viền cỏ thấp mềm thay vì vài ngọn cao lẻ tẻ.
  f32 edge_shrink = 0.0f;
  /// Nhiễu chia cỏ thành đám (tính theo mét), dùng khi `patchiness` > 0.
  noise_desc patches{.seed = 3, .frequency = 0.06f, .octaves = 2};
  f32 patchiness = 0.4f;      ///< 0 là mọc đều; 1 là thành đám rõ, giữa các đám trống.
  /// Nhiễu chia cỏ thành vùng cao thấp (tính theo mét), dùng khi `height_variation` > 0.
  noise_desc height_patches{.seed = 7, .frequency = 0.08f, .octaves = 2};
  /// Mỗi vùng cao thấp hơn `height` tới tỉ lệ này: 0.5 là vùng thấp nhất cao nửa `height`,
  /// cao nhất gấp rưỡi. `height_jitter` vẫn làm từng ngọn trong vùng khác nhau.
  f32 height_variation = 0.0f;
  rgba tint_color{0.62f, 0.70f, 0.24f, 1.0f}; ///< Màu pha vào từng mảng cỏ theo `tint_patches`.
  noise_desc tint_patches{.seed = 9, .frequency = 0.05f, .octaves = 2}; ///< Nhiễu chia mảng màu (tính theo mét).
  f32 tint = 0.0f;            ///< Pha `tint_color` nhiều nhất bấy nhiêu, 0..1. 0 là một màu.
  /// Ngọn cong sẵn sang một bên (không cần gió), 0..1: cỏ mềm, tròn, kiểu vẽ tay.
  f32 curve = 0.0f;
  /// Ngọn lấy màu đất nơi nó mọc, 0..1 (0 là tắt): gốc ngọn mang màu địa hình tại gốc
  /// (ảnh và màu các lớp, theo trọng số lớp) thay cho `base_color`, lên ngọn chuyển dần
  /// sang `tip_color`. Thảm cỏ liền với mặt đất, không thành gai tối trên nền.
  f32 ground_color = 0.0f;
  /// Gốc ngọn tối còn bấy nhiêu lần màu của nó, 0..1 (1 là không tối): cỏ tự che gốc mình.
  f32 root_shade = 0.6f;
  f32 draw_distance = 60.0f;  ///< Xa hơn khoảng này không có cỏ, mét.
  f32 fade = 20.0f;           ///< Cỏ thấp dần trong khoảng này trước `draw_distance`, mét.
  /// Xa hơn tầm vẽ, mặt đất chỗ cỏ mọc giả làm thảm cỏ, 0..1 (0 là tắt): địa hình lấy
  /// màu của thảm nhìn từ xa (`tip_color` pha `tint_color`) với những đốm bụi cỏ, đổi dần
  /// trong khoảng `fade` khi ngọn thấp đi. Gần như không tốn (vài phép tính trong shader
  /// địa hình), nên nhìn từ thật xa thảm cỏ không thành đất trọc.
  f32 far_cover = 0.0f;
  f32 sway = 1.0f;           ///< Đung đưa theo gió nhiều hay ít. 0 là đứng yên.
  u32 seed = 1;               ///< Hạt giống: cùng hạt giống thì cỏ mọc y như nhau.
  bool cast_shadows = false;  ///< Ngọn cỏ đổ bóng (tốn: mọi ngọn vẽ thêm vào ảnh bóng). Luôn nhận bóng.
};

/// Tạo thảm cỏ. Cỏ tự mọc lại theo vùng khi địa hình bị sửa (terrain3d_edit()).
/// @param ctx Context của engine.
/// @param desc Cách mọc.
/// @return Handle, hoặc handle không hợp lệ nếu địa hình không hợp lệ.
grass3d_handle grass3d_create(context &ctx, const grass3d_desc &desc);

/// Hủy thảm cỏ.
/// @param ctx Context của engine.
/// @param handle Thảm cỏ. Handle không hợp lệ bị bỏ qua.
void grass3d_destroy(context &ctx, grass3d_handle handle);

/// Vẽ cỏ quanh camera của lần vẽ 3D đang mở.
/// @param ctx Context của engine.
/// @param handle Thảm cỏ.
void draw_grass3d(const context &ctx, grass3d_handle handle);

/// Đổi cách mọc của thảm cỏ (cỏ theo mùa: xanh non, vàng úa, phủ tuyết). Màu
/// (`base_color`, `tip_color`, `tint_color`), độ mạnh của `ground_color`,
/// `root_shade`, `curve`, `sway`, `far_cover`, tầm vẽ và bóng đổ đổi ngay ở lần vẽ
/// sau, không tốn gì: đổi được mỗi frame. Đổi những gì định ngọn mọc ở đâu và
/// cao, rộng ra sao (`density`, `height`, `layer`, nhiễu, `tint`, `color_jitter`,
/// `seed`..., hay bật tắt `ground_color` qua 0) thì cỏ mọc lại hết: tốn như lúc
/// mới tạo, đừng làm mỗi frame. Địa hình giữ như cũ (`terrain` mới bị bỏ qua).
/// Màu đất ở gốc ngọn (`ground_color`) lấy lúc ngọn mọc, nên không theo màu lớp
/// địa hình đổi sau đó.
/// @param ctx Context của engine.
/// @param handle Thảm cỏ. Handle không hợp lệ bị bỏ qua.
/// @param desc Cách mọc mới.
void grass3d_set(context &ctx, grass3d_handle handle, const grass3d_desc &desc);

/// Cách mọc hiện tại của thảm cỏ.
/// @param ctx Context của engine.
/// @param handle Thảm cỏ.
/// @return Cách mọc, hoặc mặc định nếu handle không hợp lệ.
grass3d_desc grass3d_get(const context &ctx, grass3d_handle handle);

/// Số ngọn cỏ vẽ ở lần vẽ 3D gần nhất, để đo.
/// @param ctx Context của engine.
/// @param handle Thảm cỏ.
/// @return Số ngọn.
i32 grass3d_drawn(const context &ctx, grass3d_handle handle);

/// Đặt gió cho cỏ, mây và mưa: vận tốc trên mặt xz, mét mỗi giây. draw_sky3d() đặt lại
/// bằng `weather3d::wind` mỗi frame.
/// @param ctx Context của engine.
/// @param wind Vận tốc gió `(x, z)`.
void wind3d_set(context &ctx, vec2 wind);

/// Gió đang dùng.
/// @param ctx Context của engine.
/// @return Vận tốc gió `(x, z)`, mét mỗi giây.
vec2 wind3d_get(const context &ctx);

/// Cách rải một loại vật (đá, cây, bụi) lên địa hình (scatter3d_create()).
///
/// Chỗ đặt được chọn một lần khi tạo, theo hạt giống: đủ xa nhau (`spacing`), trong
/// khoảng độ cao, độ dốc và lớp cho phép. Vật được vẽ bằng instancing theo từng vùng,
/// bỏ vùng ngoài tầm nhìn, dùng `far_model` cho vùng xa hơn `lod_distance` và không vẽ
/// gì xa hơn `draw_distance`.
struct scatter3d_desc {
  terrain3d_handle terrain{}; ///< Địa hình.
  model_handle model{};       ///< Model vẽ ở gần.
  model_handle far_model{};   ///< Model đơn giản hơn cho chỗ xa. Không hợp lệ là dùng `model`.
  f32 density = 0.02f;        ///< Số vật mỗi mét vuông nơi dày nhất.
  f32 spacing = 2.0f;         ///< Hai vật cách nhau ít nhất bấy nhiêu mét.
  f32 min_height = -1.0e9f;   ///< Chỉ đặt chỗ cao từ mức này, mét.
  f32 max_height = 1.0e9f;    ///< Chỉ đặt chỗ thấp hơn mức này, mét.
  f32 min_slope = 0.0f;       ///< Độ dốc nhỏ nhất, độ.
  f32 max_slope = 30.0f;      ///< Độ dốc lớn nhất, độ.
  i32 layer = -1;             ///< Chỉ đặt trên lớp này (trọng số từ `layer_min`); -1 là mọi lớp.
  f32 layer_min = 0.5f;       ///< Trọng số tối thiểu của `layer`.
  noise_desc patches{.seed = 5, .frequency = 0.02f, .octaves = 2}; ///< Nhiễu chia thành cụm, khi `patchiness` > 0.
  f32 patchiness = 0.0f;      ///< 0 là rải đều; 1 là thành cụm (rừng, bãi đá).
  f32 scale_min = 0.8f;       ///< Tỉ lệ nhỏ nhất.
  f32 scale_max = 1.2f;       ///< Tỉ lệ lớn nhất.
  f32 align = 0.0f;           ///< 0 là đứng thẳng (cây); 1 là nghiêng theo mặt đất (đá).
  f32 sink = 0.0f;            ///< Lún xuống đất bấy nhiêu mét (chân đá không lơ lửng trên dốc).
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nhân vào model.
  f32 color_jitter = 0.1f;    ///< Mỗi vật sáng tối khác nhau tới tỉ lệ này.
  f32 lod_distance = 80.0f;   ///< Xa hơn thì dùng `far_model`, mét.
  f32 draw_distance = 300.0f; ///< Xa hơn thì không vẽ, mét.
  u32 seed = 1;               ///< Hạt giống.
};

/// Rải vật lên địa hình.
/// @param ctx Context của engine.
/// @param desc Cách rải.
/// @return Handle, hoặc handle không hợp lệ nếu địa hình hay model không hợp lệ.
scatter3d_handle scatter3d_create(context &ctx, const scatter3d_desc &desc);

/// Hủy lớp vật rải.
/// @param ctx Context của engine.
/// @param handle Lớp vật rải. Handle không hợp lệ bị bỏ qua.
void scatter3d_destroy(context &ctx, scatter3d_handle handle);

/// Vẽ các vật rải trong tầm nhìn của lần vẽ 3D đang mở (bằng draw_instanced3d()).
/// @param ctx Context của engine.
/// @param handle Lớp vật rải.
void draw_scatter3d(const context &ctx, scatter3d_handle handle);

/// Số vật đã rải.
/// @param ctx Context của engine.
/// @param handle Lớp vật rải.
/// @return Số vật.
i32 scatter3d_count(const context &ctx, scatter3d_handle handle);

/// Vị trí, hướng và tỉ lệ của các vật đã rải, để game thêm va chạm (body3d_create())
/// cho cây hay đá to.
/// @param ctx Context của engine.
/// @param handle Lớp vật rải.
/// @param out Mảng nhận, hoặc nullptr để chỉ đếm.
/// @param count Số phần tử của `out`.
/// @return Số vật (có thể lớn hơn `count`: chỉ `count` vật đầu được ghi).
i32 scatter3d_transforms(const context &ctx, scatter3d_handle handle, transform3d *out, i32 count);

// ---------------------------------------------------------------------------
// Nước
// ---------------------------------------------------------------------------

/// Số sóng tối đa của một mặt nước.
inline constexpr i32 water3d_wave_max = 8;

/// Một con sóng Gerstner: đỉnh nhọn, đáy bẹt, nước chuyển động theo vòng tròn như sóng
/// thật. Tốc độ theo bước sóng như sóng nước sâu (sóng dài đi nhanh hơn).
struct water3d_wave {
  vec2 direction{1.0f, 0.0f}; ///< Hướng sóng đi trên mặt xz (không cần độ dài 1).
  f32 wavelength = 12.0f;     ///< Khoảng cách hai đỉnh sóng, mét.
  /// Độ nhọn, 0..1: biên độ là `steepness * wavelength / (2 pi)`. Tổng độ nhọn các
  /// sóng nên dưới 1, không thì đỉnh sóng tự gập.
  f32 steepness = 0.2f;
  f32 speed = 1.0f;           ///< Nhân vào tốc độ tự nhiên của sóng.
};

/// Cách dựng một mặt nước (water3d_create()).
///
/// `size` khác 0 là một hồ hình chữ nhật tâm `center`. `size` bằng 0 là biển trải tới
/// chân trời: lưới đi theo camera, dày ở gần và thưa ở xa.
///
/// Màu nước theo độ sâu (từ `shallow_color` tới `deep_color`), trong suốt chỗ nông,
/// có bọt ở bờ và trên đỉnh sóng. Mặt nước phản chiếu bầu trời (của draw_sky3d() nếu
/// có trong lần vẽ, không thì theo màu sương và ánh sáng nền), nắng lấp lánh và nhận
/// bóng đổ. Độ sâu và bờ tính theo `terrain`: không có địa hình thì nước coi như sâu
/// đều và không có bờ.
struct water3d_desc {
  f32 level = 0.0f;                ///< Độ cao mặt nước yên, mét.
  vec2 center{0.0f, 0.0f};         ///< Tâm hồ trên mặt xz.
  vec2 size{0.0f, 0.0f};           ///< Kích thước hồ theo x và z, mét; `{0, 0}` là biển.
  /// Các con sóng. Mặc định ba sóng nhẹ cho mặt hồ có gió.
  water3d_wave waves[water3d_wave_max]{{{1.0f, 0.3f}, 9.0f, 0.18f, 1.0f},
                                       {{0.6f, -0.8f}, 5.5f, 0.14f, 1.0f},
                                       {{-0.4f, 1.0f}, 3.1f, 0.10f, 1.0f}};
  i32 wave_count = 3;              ///< Số sóng dùng, 0..njin::water3d_wave_max.
  rgba shallow_color{0.10f, 0.42f, 0.45f, 1.0f}; ///< Màu chỗ nông.
  rgba deep_color{0.02f, 0.09f, 0.16f, 1.0f};    ///< Màu chỗ sâu.
  f32 depth_fade = 6.0f;           ///< Sâu bấy nhiêu mét thì gần như là `deep_color`.
  f32 clarity = 1.5f;              ///< Nông hơn bấy nhiêu mét thì nhìn xuyên thấy đáy.
  rgba foam_color{0.95f, 0.97f, 1.0f, 1.0f};     ///< Màu bọt.
  f32 foam_width = 0.5f;           ///< Bọt ở bờ trong độ sâu này, mét. 0 là không có.
  f32 crest_foam = 0.35f;          ///< Bọt trên đỉnh sóng cao, 0..1.
  f32 ripples = 0.35f;             ///< Gợn lăn tăn nhỏ trên mặt sóng, 0..1.
  f32 specular = 1.0f;             ///< Độ mạnh của nắng lấp lánh.
  f32 shininess = 300.0f;          ///< Độ nhọn của nắng lấp lánh.
  terrain3d_handle terrain{};      ///< Địa hình làm đáy và bờ.
  /// Độ dày lưới, nhân với mặc định. Lớn thì sóng mịn hơn ở xa, tốn hơn.
  f32 detail = 1.0f;
  /// Không có `terrain`: đáy phẳng sâu bấy nhiêu mét dưới `level`, để nước nông
  /// trong thấy đáy (chậu, bồn rửa, bể nhỏ). 0 (mặc định) là sâu đều như biển.
  f32 bed = 0.0f;
  /// Đèn điểm và đèn nón (light3d_add(), njin::light3d_source) cũng chiếu lên mặt
  /// nước và lấp lánh trên nó, cho nước trong nhà hay ban đêm. Mặc định chỉ có
  /// mặt trời.
  bool lamps = false;
  /// Một dòng nước rơi xuống mặt nước (vòi, máng xối, thác nhỏ) tại `pour_at` trên
  /// mặt xz: gợn tròn lan ra từ đó, nhạt dần tới `pour_reach` mét, bọt sủi chỗ nước
  /// rơi. Chỉ là hình vẽ: water3d_height() và water3d_normal() không tính gợn này.
  vec2 pour_at{0.0f, 0.0f};
  f32 pour = 0.0f;       ///< Độ mạnh của dòng nước, 0..1. 0 (mặc định) là không có.
  f32 pour_reach = 0.3f; ///< Gợn lan xa bấy nhiêu mét; mười vòng gợn trong tầm này.
  /// Hồ bầu dục vừa trong `size` thay cho hình chữ nhật, mép lượn theo nhiễu và
  /// trong dần ra mép: vũng nước loang trên sàn, ao nhỏ. Bỏ qua với biển.
  bool round = false;
  /// Khúc xạ: nhìn qua mặt nước, những gì dưới nước cong lệch theo sóng và gợn,
  /// lệch nhiều hơn chỗ nước dày; độ sâu thật tới đáy và tới vật dưới nước lấy
  /// theo cảnh đã vẽ (thay cho `bed` và `terrain`), nên nước quanh một vật chìm
  /// cũng trong hơn. Độ mạnh, khoảng 0..1; 0 (mặc định) là tắt. Bật nó làm lần vẽ
  /// 3D chép ảnh màu và độ sâu của cảnh một lần mỗi frame, như post3d.
  f32 refract = 0.0f;
  /// Vân sáng dưới đáy (caustics): sóng và gợn khúc xạ nắng, gom lại thành lưới vệt
  /// sáng chạy trên đáy chỗ nước nông, mép vệt tách màu nhẹ. Chỉ nơi đáy có nắng,
  /// nhạt dần theo độ sâu (`depth_fade`) và ở xa. Độ mạnh, khoảng 0..1; 0 (mặc định)
  /// là tắt. Đẹp nhất cùng `refract`: vân nằm trên chính đáy nhìn qua nước.
  f32 caustics = 0.0f;
  f32 caustic_size = 1.0f; ///< Cỡ một ô của lưới vân sáng, mét.
  /// Phản chiếu cảnh: bờ, đồi, cây in ngược trên mặt nước, méo theo sóng và gợn,
  /// lấy theo cảnh đã vẽ trên màn hình; chỗ ấy không có trên màn hình thì phản chiếu
  /// bầu trời như khi tắt. Độ mạnh, 0..1: nhỏ là đúng như nước thật (nhìn gần thẳng
  /// xuống thì thấy ít), 1 là đậm như tranh vẽ. 0 (mặc định) là tắt. Bật nó, như
  /// `refract`, làm lần vẽ 3D chép ảnh màu và độ sâu của cảnh một lần mỗi frame.
  f32 reflect = 0.0f;
  /// Với `refract`: phần ánh sáng mỗi màu còn lại sau khi đi qua một mét nước. Đáy
  /// và vật chìm nhìn qua nước sẫm và ngả màu dần theo quãng ánh sáng đi xuống rồi
  /// lên (nước ao xanh rêu, nước suối ngả lục, nước biển ngả lam). Trắng (mặc định)
  /// là không nhuộm.
  rgba absorb{1.0f, 1.0f, 1.0f, 1.0f};
  /// Mưa rơi trên mặt nước, 0..1 (0 là tắt, mặc định): khi trời mưa (weather3d::rain
  /// của draw_sky3d()), giọt mưa rơi khắp mặt nước, mỗi giọt một vòng gợn lan ra rồi
  /// tắt trong chưa tới một giây, dày theo mưa; ở xa, nơi vòng nhỏ hơn điểm ảnh, mặt
  /// nước chỉ lăn tăn. Số này nhân với độ mưa. Chỉ là hình vẽ, như `pour`.
  f32 rain_rings = 0.0f;
  /// Băng, 0..1 (0 là không, mặc định): mặt nước đóng băng từ bờ ra, chỗ nông trước
  /// (theo `terrain` hay `bed`; nước sâu đều thì đóng từ khoảng 0.8), 1 là kín mặt.
  /// Băng phẳng, đục, có vân sương, phản chiếu trời nhạt; sóng lặng dần theo băng
  /// (cả water3d_height() và water3d_normal()). Mưa, gợn, bọt không có trên băng.
  f32 ice = 0.0f;
  rgba ice_color{0.78f, 0.86f, 0.92f, 1.0f}; ///< Màu băng.
  /// Tuyết nằm trên băng, 0..1: những mảng trắng khép dần lại khi dày, như
  /// njin::terrain3d_desc::snow. Chỉ chỗ đã đóng băng.
  f32 ice_snow = 0.0f;
};

/// Tạo mặt nước.
/// @param ctx Context của engine.
/// @param desc Cách dựng.
/// @return Handle.
water3d_handle water3d_create(context &ctx, const water3d_desc &desc);

/// Hủy mặt nước. Vật đang nổi trên nó thôi nổi.
/// @param ctx Context của engine.
/// @param handle Mặt nước. Handle không hợp lệ bị bỏ qua.
void water3d_destroy(context &ctx, water3d_handle handle);

/// Đổi cách dựng của mặt nước (sóng to lên khi bão, mực nước dâng).
/// @param ctx Context của engine.
/// @param handle Mặt nước.
/// @param desc Cách dựng mới.
void water3d_set(context &ctx, water3d_handle handle, const water3d_desc &desc);

/// Cách dựng hiện tại.
/// @param ctx Context của engine.
/// @param handle Mặt nước.
/// @return Cách dựng, hoặc mặc định nếu handle không hợp lệ.
water3d_desc water3d_get(const context &ctx, water3d_handle handle);

/// Số gợn tối đa cùng lúc trên một mặt nước (water3d_splash()).
inline constexpr i32 water3d_splash_max = 8;

/// Một vật rơi xuống nước (hòn đá, cái chén, giọt nước lớn) tại `at` trên mặt xz:
/// mặt nước sủi bọt ngay chỗ rơi rồi một chuỗi vòng sóng lan ra và tắt dần trong
/// vài giây. Vật càng to (`size`, mét) gợn càng dài và lan càng nhanh; `strength`
/// (0..1) là độ cao của sóng, tăng theo tốc độ vật rơi.
///
/// Chỉ là hình vẽ, như `water3d_desc::pour`: water3d_height() không tính gợn này.
/// Quá njin::water3d_splash_max gợn đang lan thì gợn cũ nhất bị thay.
/// @code
/// // Chén rơi từ 40 cm xuống: chạm nước ở khoảng 2.8 m/s.
/// njin::water3d_splash(ctx, basin, {bowl.x, bowl.z}, 2.8f / 4.0f, 0.16f);
/// @endcode
/// @param ctx Context của engine.
/// @param handle Mặt nước. Handle không hợp lệ bị bỏ qua.
/// @param at Chỗ vật rơi xuống, toạ độ x, z.
/// @param strength Độ mạnh, 0..1.
/// @param size Cỡ của vật, mét.
void water3d_splash(context &ctx, water3d_handle handle, vec2 at, f32 strength, f32 size);

/// Vẽ mặt nước trong lần vẽ 3D đang mở. Nước vẽ sau mọi hình đục, như kính.
/// @param ctx Context của engine.
/// @param handle Mặt nước.
void draw_water3d(const context &ctx, water3d_handle handle);

/// Độ cao mặt nước tại `(x, z)` vào lúc này, theo đúng công thức sóng của shader.
/// Ngoài hồ thì là `level`.
/// @param ctx Context của engine.
/// @param handle Mặt nước.
/// @param x Toạ độ x, mét.
/// @param z Toạ độ z, mét.
/// @return Độ cao, mét; 0 nếu handle không hợp lệ.
f32 water3d_height(const context &ctx, water3d_handle handle, f32 x, f32 z);

/// Pháp tuyến mặt nước tại `(x, z)` vào lúc này.
/// @param ctx Context của engine.
/// @param handle Mặt nước.
/// @param x Toạ độ x, mét.
/// @param z Toạ độ z, mét.
/// @return Pháp tuyến (độ dài 1).
vec3 water3d_normal(const context &ctx, water3d_handle handle, f32 x, f32 z);

/// Đồng hồ của sóng, giây: chạy theo delta(), nên dừng khi game tạm dừng.
/// @param ctx Context của engine.
/// @return Thời gian của sóng.
f32 water3d_time(const context &ctx);

/// Cách một body nổi trên nước (water3d_float()).
struct buoyancy3d {
  /// Lực nổi: 1 là lơ lửng, lớn hơn 1 là nổi (gỗ, thuyền), nhỏ hơn 1 là chìm dần.
  f32 buoyancy = 1.4f;
  f32 linear_drag = 0.6f;  ///< Nước cản chuyển động.
  f32 angular_drag = 0.15f; ///< Nước cản xoay.
  vec3 flow{0.0f, 0.0f, 0.0f}; ///< Dòng chảy kéo vật theo, mét mỗi giây.
};

/// Cho body động nổi trên mặt nước: mỗi bước vật lý, phần thể tích dưới mặt sóng (tại
/// tâm của body) đẩy nó lên, và nước cản nó lại, nên thùng gỗ dập dềnh theo sóng. Gọi
/// lại với cùng body để đổi `buoyancy`.
/// @param ctx Context của engine.
/// @param water Mặt nước.
/// @param body Body động.
/// @param buoyancy Lực nổi và lực cản.
void water3d_float(context &ctx, water3d_handle water, body3d_handle body, const buoyancy3d &buoyancy = {});

/// Thôi cho body nổi trên mặt nước.
/// @param ctx Context của engine.
/// @param water Mặt nước.
/// @param body Body.
void water3d_unfloat(context &ctx, water3d_handle water, body3d_handle body);

// ---------------------------------------------------------------------------
// Bầu trời và thời tiết
// ---------------------------------------------------------------------------

/// Thời tiết: mây, sương, mưa, tuyết, gió, mặt đất ướt. Đặt vào njin::sky3d.
struct weather3d {
  f32 clouds = 0.3f;         ///< Phần trời có mây, 0..1. 1 là u ám, che mặt trời.
  f32 cloud_darkness = 0.0f; ///< Mây tối (mây mưa), 0..1.
  f32 fog = 0.0f;            ///< Mật độ sương, như `light3d::fog_density`. 0 là không sương.
  f32 rain = 0.0f;           ///< Mưa, 0..1.
  f32 snow = 0.0f;           ///< Tuyết rơi, 0..1.
  vec2 wind{2.0f, 0.6f};     ///< Gió trên mặt xz, mét mỗi giây: mây trôi, cỏ nghiêng, mưa xiên.
  f32 wetness = 0.0f;        ///< Mặt đất ướt (tối và bóng hơn), 0..1.
  /// Mưa và tuyết không rơi dưới mái: mỗi frame có mưa hay tuyết, engine vẽ độ sâu của
  /// mọi hình đục đổ bóng nhìn thẳng từ trên xuống quanh camera (36 m mỗi cạnh, ô 14 cm),
  /// rồi giấu giọt nằm dưới một bề mặt và giữ đất dưới đó khô. Hình không đổ bóng
  /// (`material3d::cast_shadows` tắt) không che. Tắt để tự đánh dấu chỗ có mái bằng
  /// weather3d_cover_set(), hay khi không có gì để che (tiết kiệm một lượt vẽ độ sâu).
  bool cover_auto = true;
};

/// Một chỗ có mái, cho weather3d_cover_set(): hình hộp thẳng trục. Mưa và tuyết không
/// rơi trong phần của hộp nằm dưới mặt trên của nó (cả phía dưới đáy hộp), mờ dần
/// trong 25 cm quanh mép; đất trong đó không ướt.
struct weather3d_cover {
  vec3 center{0.0f, 0.0f, 0.0f}; ///< Tâm hộp.
  vec3 size{1.0f, 1.0f, 1.0f};   ///< Cỡ hộp, mét.
};

/// Đặt các chỗ có mái (thay danh sách cũ), thêm vào phần weather3d::cover_auto che:
/// nhà không đổ bóng, mái hiên, hay cả khi tắt `cover_auto`. Tối đa 16 hộp; hộp có
/// vị trí hay cỡ không phải số hữu hạn bị bỏ.
/// @param ctx Context của engine.
/// @param covers Các hộp, hoặc nullptr khi `count` là 0 (xóa hết).
/// @param count Số hộp.
void weather3d_cover_set(context &ctx, const weather3d_cover *covers, i32 count);

/// Số chỗ có mái đang đặt bằng weather3d_cover_set().
/// @param ctx Context của engine.
/// @return Số hộp, 0..16.
i32 weather3d_cover_count(const context &ctx);

/// Hình bông tuyết cho tuyết rơi, thay chấm tròn mờ: một ảnh chia lưới `columns` x
/// `rows` ô bằng nhau, mỗi ô một bông nằm giữa ô, kênh alpha là hình bông (màu lấy từ
/// ánh sáng, nên vẽ bông màu trắng). Mỗi bông lấy ngẫu nhiên một ô và xoay chậm khi
/// rơi. Bật mipmap cho ảnh (texture_set_filter() với njin::filter_mipmap) để bông ở xa
/// không nhấp nháy. Ảnh trong atlas (atlas_load()) không dùng được.
/// @param ctx Context của engine.
/// @param flakes Ảnh bông, hoặc handle không hợp lệ để quay về chấm tròn.
/// @param columns Số cột của lưới, 1..16.
/// @param rows Số hàng của lưới, 1..16.
/// @param sparkle Độ lấp lánh của bông gần camera (trong khoảng 6 m), 0..4: mỗi bông lóe
///                sáng chốc lát khi một mặt tinh thể xoay đón ánh sáng, kèm một ngôi sao
///                bốn cánh mảnh (với light3d::hdr thì sáng quá trắng để bloom tỏa ra). 0 là tắt.
void weather3d_snow_flakes_set(context &ctx, texture_handle flakes, i32 columns, i32 rows, f32 sparkle = 0.0f);

/// Các kiểu thời tiết có sẵn cho weather3d_preset().
enum weather3d_kind {
  weather3d_clear,    ///< Trời quang, ít mây.
  weather3d_overcast, ///< U ám, không có nắng gắt.
  weather3d_rain,     ///< Mưa, đất ướt.
  weather3d_snow,     ///< Tuyết rơi.
  weather3d_fog,      ///< Sương mù dày.
};

/// Một kiểu thời tiết có sẵn. Là giá trị thường, sửa thoải mái trước khi dùng.
/// @param kind Kiểu.
/// @return Thời tiết.
weather3d weather3d_preset(weather3d_kind kind);

/// Nội suy giữa hai thời tiết, để trời chuyển mưa dần dần.
/// @param a Thời tiết ở `t = 0`.
/// @param b Thời tiết ở `t = 1`.
/// @param t Tiến độ, 0..1.
/// @return Thời tiết ở giữa.
weather3d weather3d_lerp(const weather3d &a, const weather3d &b, f32 t);

/// Bầu trời theo giờ trong ngày, dùng với draw_sky3d().
///
/// Mặt trời mọc ở phía đông (`+x`), lên cao nhất lúc 12 giờ ở phía nam (`+z`) rồi lặn
/// ở phía tây, theo vĩ độ và mùa. Ban đêm có trăng (ánh sáng xanh nhạt, ngược phía
/// mặt trời) và sao.
struct sky3d {
  f32 hour = 10.0f;         ///< Giờ trong ngày, 0..24 (lẻ được: 6.5 là 6 giờ 30).
  f32 latitude = 35.0f;     ///< Vĩ độ, độ: càng xa xích đạo mặt trời càng thấp.
  /// Mùa, -1 (giữa đông) đến 1 (giữa hè): ngày dài hay ngắn, mặt trời cao hay thấp.
  f32 season = 0.0f;
  f32 north = 0.0f;         ///< Xoay cả bầu trời quanh trục y, độ (hướng bắc của bản đồ).
  weather3d weather{};      ///< Thời tiết.
  f32 cloud_height = 900.0f; ///< Độ cao lớp mây, mét.
  f32 cloud_scale = 1.0f;   ///< Cỡ đám mây; lớn là đám to hơn.
  bool stars = true;        ///< Có sao ban đêm.
  f32 sun_size = 1.0f;      ///< Cỡ mặt trời trên trời, nhân với cỡ thật.
  /// draw_sky3d() đặt ánh sáng của lần vẽ (light3d_set()) theo trời: hướng nắng (hay
  /// trăng), màu nắng, ánh sáng nền, màu và mật độ sương; giữ cài đặt bóng đổ của
  /// ánh sáng hiện tại. Tắt thì game tự gọi sky3d_light().
  bool drive_light = true;
};

/// Hướng từ cảnh tới mặt trời (độ dài 1). `y` âm là mặt trời đã lặn.
/// @param sky Bầu trời.
/// @return Hướng tới mặt trời.
vec3 sky3d_sun_direction(const sky3d &sky);

/// Ánh sáng theo bầu trời: `base` với hướng và màu của nắng (ban đêm là trăng), ánh
/// sáng nền theo màu trời, màu sương bằng màu chân trời, và sương của thời tiết (lấy cái
/// dày hơn giữa `base` và thời tiết). Bóng đổ giữ như `base`.
/// @param sky Bầu trời.
/// @param base Ánh sáng gốc (cài đặt bóng đổ).
/// @return Ánh sáng.
light3d sky3d_light(const sky3d &sky, const light3d &base = {});

/// Màu trời theo hướng nhìn `direction` (không có mây và sao), như draw_sky3d() vẽ.
/// Cho nền 2D, màu xóa màn hình, hay biết trời đang sáng hay tối.
/// @param sky Bầu trời.
/// @param direction Hướng nhìn.
/// @return Màu (alpha 1).
rgba sky3d_color(const sky3d &sky, vec3 direction);

/// Vẽ bầu trời (nền, mặt trời, mây, sao) phía sau mọi thứ của lần vẽ 3D đang mở, và mưa
/// hay tuyết quanh camera theo thời tiết. Đặt gió (wind3d_set()) và độ ướt của mặt đất;
/// với `sky3d::drive_light` thì đặt cả ánh sáng của lần vẽ.
///
/// Gọi ở bất kỳ đâu giữa begin_3d() và end_3d(): trời chỉ phủ chỗ chưa có gì vẽ.
/// @param ctx Context của engine.
/// @param sky Bầu trời.
void draw_sky3d(context &ctx, const sky3d &sky);
/// @}
} // namespace njin
