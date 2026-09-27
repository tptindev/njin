#pragma once
#include "_types.h"
#include <cstdio>
#include <cstdlib>

// Usage:
//   NJIN_INFO("player spawned at %.1f, %.1f", pos.x, pos.y);
//   NJIN_WARN("texture missing: %s", path);
//   njin::log_set_level(njin::log_warn);  // hide trace/debug/info
//
// Output: [  1.234] WARN  file.cpp:42  message
// The window/audio backend's own messages go through the same logger, tagged
// "njin" like the engine's own: njin seals it off from game code (see
// src/engine/guard/), so the log should not name it either.

/// @cond INTERNAL
#if defined(__MINGW_PRINTF_FORMAT)
#define NJIN_PRINTF(fmt, args) __attribute__((format(__MINGW_PRINTF_FORMAT, fmt, args)))
#elif defined(__GNUC__) || defined(__clang__)
#define NJIN_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define NJIN_PRINTF(fmt, args)
#endif
/// @endcond

namespace njin {
/// @addtogroup grp_log
/// @{

/// Mức độ của một dòng log, từ chi tiết nhất đến nghiêm trọng nhất.
enum log_level {
  log_trace, ///< Chi tiết nhất, thường chỉ dùng khi lần theo lỗi.
  log_debug, ///< Thông tin để gỡ lỗi.
  log_info,  ///< Sự kiện bình thường.
  log_warn,  ///< Có vấn đề nhưng chương trình vẫn chạy tiếp.
  log_error, ///< Một thao tác đã thất bại.
  log_fatal, ///< Lỗi không thể tiếp tục. NJIN_FATAL() dừng chương trình.
  log_off    ///< Chỉ dùng với log_set_level() để tắt toàn bộ log.
};

/// Hàm nhận mọi dòng log vượt qua bộ lọc mức độ.
///
/// `line` là 0 khi không biết đúng dòng, chỉ có tên nguồn (`file` là `"njin"`
/// cho log của window/audio backend); cả `file` cũng là nullptr khi không
/// biết gì. `user` là con trỏ đã truyền cho log_set_sink().
using log_sink = void (*)(log_level level, const char *file, i32 line,
                          const char *msg, void *user);

/// Bỏ qua các dòng log có mức thấp hơn `level`.
///
/// Mặc định là log_debug ở bản debug và log_info khi có `NDEBUG`.
/// @param level Mức tối thiểu được giữ lại.
void log_set_level(log_level level);

/// Mức tối thiểu hiện tại.
/// @return Mức tối thiểu hiện tại.
log_level log_get_level();

/// Đúng nếu một dòng log ở mức `level` sẽ được ghi.
/// @param level Mức cần kiểm tra.
/// @return `true` nếu dòng log ở mức đó sẽ được ghi.
bool log_enabled(log_level level);

/// Thay nơi nhận log. Truyền nullptr để trở về mặc định (stderr).
/// @param sink Hàm nhận log, hoặc nullptr.
/// @param user Con trỏ tùy ý, được truyền lại cho `sink`.
void log_set_sink(log_sink sink, void *user = nullptr);

/// Tên của một mức, ví dụ "WARN".
/// @param level Mức cần lấy tên.
/// @return Chuỗi có sẵn, không cần giải phóng.
const char *log_level_name(log_level level);

/// Ghi một dòng log theo định dạng printf.
///
/// Nên dùng các macro NJIN_* bên dưới vì chúng tự điền `file` và `line`.
/// @param level Mức của dòng log.
/// @param file Tên file nguồn, hoặc nullptr.
/// @param line Số dòng, hoặc 0.
/// @param fmt Chuỗi định dạng kiểu printf.
void log_write(log_level level, const char *file, i32 line, const char *fmt,
               ...) NJIN_PRINTF(4, 5);
/// @}
} // namespace njin

/// @addtogroup grp_log
/// @{

/// Ghi log ở mức `level`, tự điền file và dòng. Các macro bên dưới gọn hơn.
#define NJIN_LOG(level, ...)                                                   \
  ::njin::log_write((level), __FILE__, __LINE__, __VA_ARGS__)

/// Ghi log mức trace. Tham số giống printf.
#define NJIN_TRACE(...) NJIN_LOG(::njin::log_trace, __VA_ARGS__)
/// Ghi log mức debug. Tham số giống printf.
#define NJIN_DEBUG(...) NJIN_LOG(::njin::log_debug, __VA_ARGS__)
/// Ghi log mức info. Tham số giống printf.
#define NJIN_INFO(...) NJIN_LOG(::njin::log_info, __VA_ARGS__)
/// Ghi log mức warn. Tham số giống printf.
#define NJIN_WARN(...) NJIN_LOG(::njin::log_warn, __VA_ARGS__)
/// Ghi log mức error. Tham số giống printf.
#define NJIN_ERROR(...) NJIN_LOG(::njin::log_error, __VA_ARGS__)
/// Ghi log mức fatal rồi dừng chương trình bằng `std::abort()`.
#define NJIN_FATAL(...)                                                        \
  do {                                                                         \
    NJIN_LOG(::njin::log_fatal, __VA_ARGS__);                                  \
    std::abort();                                                              \
  } while (0)
/// @}
