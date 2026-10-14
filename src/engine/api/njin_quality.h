#pragma once
#include "_types.h"

namespace njin {
struct context;

/// @addtogroup grp_quality
/// @{

/// Các mức chất lượng dựng sẵn, xem graphics_quality_preset().
enum quality_level {
  quality_low,    ///< Cho máy yếu: GPU tích hợp cũ, máy không có GPU thật.
  quality_medium, ///< Cho GPU tích hợp đời mới, laptop văn phòng.
  quality_high,   ///< Không giới hạn gì: game vẽ đúng như nó đặt.
};

/// Mức trần chất lượng đồ họa, áp lên mọi thiết lập của game.
///
/// Game vẫn đặt hiệu ứng như bình thường (light3d_set(), post3d_set(),
/// post_fx_set(), lighting_set(), particle emitter, model LOD...). Struct này
/// chỉ giới hạn những gì thật sự được vẽ: tắt một hiệu ứng, hạ một độ phân
/// giải, bớt hạt. Các hàm `_get` vẫn trả về đúng giá trị game đã đặt, nên
/// menu cài đặt của game không cần biết gì về nó, và quay lại `quality_high`
/// là mọi thứ trở về như cũ.
///
/// Giá trị mặc định của mọi trường là "không giới hạn": một game không gọi
/// graphics_quality_set() vẽ y hệt như trước.
struct graphics_quality {
  /// Thế giới (pha `phase_render`) được vẽ ở bấy nhiêu phần độ phân giải màn
  /// hình theo mỗi chiều rồi phóng lên mượt; giao diện (`phase_post_render`)
  /// và chữ vẫn vẽ ở độ phân giải thật. 0.25..1, 1 là tắt. 0.5 chỉ còn một
  /// phần tư số pixel GPU phải tô. Không áp khi có độ phân giải ảo
  /// (window_set_virtual_size()): pixel art đã vẽ ở ảnh nhỏ.
  f32 world_scale = 1.0f;
  /// Bóng của mặt trời 3D (`light3d::shadows`). `false` thì tắt.
  bool shadows = true;
  /// Cạnh lớn nhất của bản đồ bóng mặt trời, pixel (`light3d::shadow_size`).
  /// 0 là không giới hạn.
  i32 shadow_size_max = 0;
  /// Bóng của đèn điểm và đèn rọi 3D (`light3d_source::shadows`). Đèn điểm vẽ
  /// lại cảnh sáu lần cho mỗi bóng, nên đây là thứ đắt nhất trên máy yếu.
  bool lamp_shadows = true;
  /// Cạnh lớn nhất của mỗi ô bóng đèn, pixel (`light3d::source_shadow_size`).
  /// 0 là không giới hạn.
  i32 lamp_shadow_size_max = 0;
  bool ssao = true;          ///< SSAO của post3d. `false` thì tắt.
  i32 ssao_samples_max = 0;  ///< Số mẫu SSAO tối đa mỗi pixel. 0 là không giới hạn.
  bool ssr = true;           ///< Phản chiếu trên màn hình của post3d.
  i32 ssr_steps_max = 0;     ///< Số bước dò SSR tối đa. 0 là không giới hạn.
  bool motion_blur = true;   ///< Mờ chuyển động của post3d.
  bool taa = true;           ///< TAA của post3d.
  bool sun_shafts = true;    ///< Tia nắng và lóa ống kính của post3d.
  bool bloom = true;         ///< Bloom của post_fx.
  bool dof = true;           ///< Độ sâu trường ảnh của post_fx.
  /// Độ phân giải lớn nhất của bản đồ ánh sáng 2D (`lighting_desc::scale`),
  /// 0.25..1. 1 là không giới hạn.
  f32 lighting_scale_max = 1.0f;
  /// Nhân vào số hạt: tốc độ phát, số hạt mỗi lần bùng và số hạt tối đa của
  /// mỗi emitter, 2D và 3D. 0..1, 1 là giữ nguyên.
  f32 particles = 1.0f;
  /// Nhân vào ngưỡng đổi LOD của model (`model_lod_desc::screen`): 2 thì
  /// model chuyển sang bản ít tam giác hơn ở khoảng cách bằng nửa. 1 là giữ
  /// nguyên. Chỉ có tác dụng với model đã dựng LOD (model_lod_build()).
  f32 lod_scale = 1.0f;
  /// Nhân vào tầm vẽ của cỏ (`grass3d_desc::draw_distance`), 0.1..1. 1 là giữ
  /// nguyên.
  f32 view_distance = 1.0f;
};

/// Trả về bộ giới hạn dựng sẵn của một mức.
///
/// `quality_high` là struct mặc định (không giới hạn gì). `quality_medium`
/// tắt SSR, TAA, mờ chuyển động và bóng đèn, hạ bóng mặt trời xuống 1024.
/// `quality_low` tắt thêm bóng mặt trời, SSAO, tia nắng, bloom, độ sâu
/// trường ảnh, vẽ thế giới ở 0.7 độ phân giải và bớt một nửa số hạt. Lấy về
/// rồi sửa từng trường nếu game muốn khác.
/// @param level Mức chất lượng.
/// @return Bộ giới hạn của mức đó.
graphics_quality graphics_quality_preset(quality_level level);

/// Đặt mức trần chất lượng đồ họa. Áp dụng ngay từ frame sau, đổi được bất cứ
/// lúc nào (menu cài đặt). Sau lần gọi đầu tiên, settings_save() lưu nó cùng
/// cài đặt của người chơi và settings_load() nạp lại.
/// @param ctx Context của engine.
/// @param quality Bộ giới hạn, thường lấy từ graphics_quality_preset().
void graphics_quality_set(context &ctx, const graphics_quality &quality);

/// Trả về bộ giới hạn đang dùng. Chưa đặt lần nào thì là struct mặc định.
/// @param ctx Context của engine.
/// @return Bộ giới hạn hiện tại.
graphics_quality graphics_quality_get(const context &ctx);

/// Đoán mức hợp với máy đang chạy, từ tên GPU mà driver OpenGL báo.
///
/// `quality_low` cho renderer phần mềm (máy không có GPU thật) và GPU tích hợp
/// cũ hay GPU di động (Intel HD/GMA, Mali, Adreno, VideoCore, PowerVR).
/// `quality_medium` cho GPU tích hợp đời mới (Intel Iris/UHD/Arc tích hợp,
/// Radeon Vega/Graphics của APU). Còn lại là `quality_high`. Chỉ là gợi ý cho
/// lần chạy đầu: hãy để người chơi đổi trong menu.
/// @param ctx Context của engine.
/// @return Mức gợi ý.
quality_level graphics_quality_suggest(const context &ctx);
/// @}
} // namespace njin
