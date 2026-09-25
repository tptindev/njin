#pragma once
#include "_types.h"
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_module
/// @{

/// Một system là một hàm thường nhận context của engine.
///
/// System được đăng ký vào một sys_phase bằng ecs_register() và được gọi mỗi
/// lần phase đó chạy.
using sys_fnc = void (*)(njin_ctx &ctx);

/// Các phase của một frame, chạy theo đúng thứ tự khai báo.
///
/// `phase_startup` chạy một lần trước frame đầu tiên và `phase_shutdown` chạy
/// một lần sau khi cửa sổ đóng. Các phase còn lại chạy mỗi frame.
///
/// `phase_pre_render`, `phase_render` và `phase_post_render` chạy giữa lúc bắt
/// đầu và kết thúc vẽ. Module camera của engine vẽ `phase_render` trong không
/// gian thế giới (qua camera đang dùng) và `phase_post_render` trong không
/// gian màn hình, nên dùng `phase_post_render` cho UI.
enum sys_phase {
  phase_startup,      ///< Một lần, trước frame đầu tiên.
  phase_pre_update,   ///< Mỗi frame, trước khi cập nhật.
  phase_update,       ///< Mỗi frame, logic chính của game.
  phase_post_update,  ///< Mỗi frame, sau khi cập nhật. Event được phát ngay sau phase này.
  phase_pre_render,   ///< Mỗi frame, trước khi vẽ thế giới.
  phase_render,       ///< Mỗi frame, vẽ trong không gian thế giới.
  phase_post_render,  ///< Mỗi frame, vẽ trong không gian màn hình (UI).
  phase_shutdown,     ///< Một lần, sau khi cửa sổ đóng.
  phase_count         ///< Số lượng phase. Không phải một phase thật.
};

/// Mô tả một system kèm ràng buộc thứ tự, dùng với ecs_register().
///
/// Chỉ so sánh với các system do cùng một module đăng ký trong cùng một phase.
struct sys_desc {
  sys_fnc fnc = nullptr; ///< Hàm system. Không được null.
  /// Giá trị nhỏ chạy trước, miễn là `after`/`before` cho phép.
  i32 order = 100;
  /// Các system phải chạy trước system này.
  std::vector<sys_fnc> after{};
  /// Các system phải chạy sau system này.
  std::vector<sys_fnc> before{};
};

/// Mô tả một module: một nhóm system có tên.
///
/// `setup` được njin_mod_register() gọi đúng một lần và phải đăng ký các
/// system của module bằng ecs_register().
struct mod_desc {
  const char *name = nullptr;          ///< Tên module, phải là duy nhất.
  void (*setup)(njin_ctx &ctx) = nullptr; ///< Đăng ký system của module.
};
/// @}
} // namespace njin
