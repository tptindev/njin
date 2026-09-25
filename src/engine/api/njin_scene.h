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
/// @}
} // namespace njin
