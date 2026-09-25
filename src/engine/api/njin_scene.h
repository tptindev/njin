#pragma once
#include "_mod.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_scene
/// @{

/// Mô tả một scene, dùng với scene_register().
///
/// Scene là một trạng thái lớn của game: menu, đang chơi, game over. Mỗi lúc
/// chỉ có một scene đang chạy.
struct scene_desc {
  const char *name = nullptr;  ///< Tên scene, phải là duy nhất.
  sys_fnc on_enter = nullptr;  ///< Gọi một lần khi vào scene. Có thể null.
  sys_fnc on_exit = nullptr;   ///< Gọi một lần khi rời scene. Có thể null.
};

/// Đăng ký một scene. Nếu tên đã có thì trả về scene cũ.
///
/// Dùng được ở mọi lúc, kể cả trong `setup` của module: đăng ký scene trước,
/// rồi dùng handle trong njin::sys_desc để system chỉ chạy ở scene đó.
/// @param ctx Context của engine.
/// @param desc Mô tả scene.
/// @return Handle của scene, hoặc handle id 0 nếu `desc.name` là null.
scene_handle scene_register(njin_ctx &ctx, const scene_desc &desc);

/// Tìm scene theo tên.
/// @param ctx Context của engine.
/// @param name Tên scene.
/// @return Handle của scene, hoặc handle id 0 nếu không có.
scene_handle scene_find(const njin_ctx &ctx, const char *name);

/// Chuyển sang một scene khác.
///
/// Việc chuyển diễn ra ở **đầu frame sau**, không phải ngay lập tức, nên frame
/// hiện tại chạy trọn vẹn. Khi chuyển, engine lần lượt:
/// 1. gọi `on_exit` của scene cũ,
/// 2. hủy mọi entity có njin::scene_owned trỏ tới scene cũ,
/// 3. gọi `on_enter` của scene mới.
///
/// Gọi nhiều lần trong một frame thì lần cuối thắng. Chuyển sang chính scene
/// đang chạy thì không làm gì.
/// @param ctx Context của engine.
/// @param scene Scene đích.
void scene_set(njin_ctx &ctx, scene_handle scene);

/// Scene đang chạy.
/// @param ctx Context của engine.
/// @return Scene đang chạy, hoặc handle id 0 nếu chưa có.
scene_handle scene_current(const njin_ctx &ctx);

/// Hiệu ứng chuyển scene, dùng với scene_fade().
///
/// Màn hình phủ dần `color` trong `fade_out` giây, đổi scene lúc đã phủ kín,
/// giữ kín ít nhất `hold` giây, rồi mở dần trong `fade_in` giây. Thời gian
/// tính theo giờ thật: không bị pause hay time_set_scale() ảnh hưởng.
struct scene_transition {
  f32 fade_out = 0.35f; ///< Thời gian phủ màn hình, giây.
  f32 hold = 0.0f;      ///< Thời gian tối thiểu giữ màn hình kín, giây.
  f32 fade_in = 0.35f;  ///< Thời gian mở màn hình, giây.
  rgba color{0.0f, 0.0f, 0.0f, 1.0f}; ///< Màu phủ. Mặc định đen.
  /// Vẽ màn hình loading trong lúc phủ kín, trong không gian màn hình, đè lên
  /// màu phủ. Có thể null.
  ///
  /// Được vẽ ít nhất một frame **trước** khi `on_enter` của scene mới chạy,
  /// nên `on_enter` nạp tài nguyên nặng thì người chơi vẫn thấy màn hình này
  /// thay vì cửa sổ đứng hình.
  sys_fnc draw_loading = nullptr;
};

/// Chuyển sang scene khác với hiệu ứng mờ dần (fade).
///
/// Giống scene_set() nhưng việc đổi scene xảy ra khi màn hình đã phủ kín. Game
/// vẫn chạy trong lúc chuyển: dùng scene_transitioning() để bỏ qua nhập liệu
/// nếu cần.
///
/// Gọi lại khi đang chuyển thì chỉ đổi scene đích (và hiệu ứng), không bắt
/// đầu lại từ đầu; nếu đang mở màn hình thì phủ lại từ độ phủ hiện tại. Gọi
/// scene_set() khi đang chuyển thì hủy hiệu ứng và đổi scene ngay frame sau.
/// @param ctx Context của engine.
/// @param scene Scene đích.
/// @param transition Hiệu ứng.
void scene_fade(njin_ctx &ctx, scene_handle scene,
                const scene_transition &transition = {});

/// Có đang chuyển scene bằng scene_fade() không.
/// @param ctx Context của engine.
/// @return `true` từ lúc gọi scene_fade() đến khi màn hình mở hết.
bool scene_transitioning(const njin_ctx &ctx);

/// Độ phủ hiện tại của hiệu ứng chuyển scene.
/// @param ctx Context của engine.
/// @return 0 là không phủ, 1 là phủ kín.
f32 scene_transition_cover(const njin_ctx &ctx);
/// @}
} // namespace njin
