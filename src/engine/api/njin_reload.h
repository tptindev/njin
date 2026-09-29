#pragma once
#include "_types.h"
#include <string>

namespace njin {
struct context;

/// @addtogroup grp_reload
/// @{

/// Một tài nguyên vừa được nạp lại, gửi qua events() sau lần nạp lại.
///
/// Handle của tài nguyên không đổi, nên thường không cần làm gì; event này
/// dành cho game muốn phản ứng (ghi log, tính lại thứ gì dựa trên kích thước ảnh).
struct asset_reloaded {
  std::string path;    ///< Đường dẫn file đã đổi.
  bool shader = false; ///< `true` là shader, `false` là texture.
  bool ok = true;      ///< `false` nếu nạp lại thất bại (ví dụ shader lỗi biên dịch) và bản cũ được giữ.
};

/// Bật hoặc tắt hot reload: sửa file ảnh hoặc shader khi game đang chạy, lưu
/// lại, và game dùng ngay bản mới, không cần khởi động lại.
///
/// Khi bật, engine kiểm tra thời gian sửa của mọi file texture và shader đã
/// nạp, vài lần mỗi giây. File đổi được nạp lại **vào đúng handle cũ**, nên
/// sprite, tilemap, shader post đang dùng nó đổi theo ngay. File vừa đổi được
/// nạp lại ở lần kiểm tra sau, khi nó đã thôi đổi, để không đọc nhầm một file
/// editor đang ghi dở.
///
/// Shader lỗi biên dịch thì **giữ bản cũ** và ghi lỗi của trình biên dịch vào
/// log, nên sửa shader sai không làm game hỏng; sửa đúng thì bản mới vào ngay.
///
/// Mặc định tắt: kiểm tra file tốn một chút thời gian mỗi lần, và game đã phát
/// hành không cần. Thường bật trong bản debug:
/// @code
/// #ifndef NDEBUG
///   njin::hot_reload_enable(*ctx, true);
/// #endif
/// @endcode
/// @param ctx Context của engine.
/// @param on Bật hay tắt.
/// @param interval Khoảng giữa hai lần kiểm tra, giây thật.
void hot_reload_enable(context &ctx, bool on, f32 interval = 0.25f);

/// Hot reload có đang bật không. @param ctx Context của engine. @return `true` nếu bật.
bool hot_reload_enabled(const context &ctx);

/// Kiểm tra ngay mọi file và nạp lại những file đã đổi, không đợi. Chạy được
/// cả khi hot reload đang tắt, ví dụ gắn vào phím F5.
/// @param ctx Context của engine.
/// @return Số tài nguyên đã nạp lại thành công.
i32 hot_reload_now(context &ctx);
/// @}
} // namespace njin
