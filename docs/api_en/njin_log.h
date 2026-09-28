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

/// Level of a log line, from most detailed to most severe.
enum log_level {
  log_trace, ///< Most detailed, usually only used when tracking down a bug.
  log_debug, ///< Information for debugging.
  log_info,  ///< Normal events.
  log_warn,  ///< Something is wrong but the program keeps running.
  log_error, ///< An operation failed.
  log_fatal, ///< Unrecoverable error. NJIN_FATAL() stops the program.
  log_off    ///< Only for log_set_level(), to turn off all logging.
};

/// Function that receives every log line that passes the level filter.
///
/// `line` is 0 when the exact line is unknown and only the source name is
/// available (`file` is `"njin"` for logs from the window/audio backend);
/// `file` is also nullptr when nothing is known. `user` is the pointer passed
/// to log_set_sink().
using log_sink = void (*)(log_level level, const char *file, i32 line,
                          const char *msg, void *user);

/// Ignore log lines below `level`.
///
/// Defaults to log_debug in debug builds and log_info when `NDEBUG` is set.
/// @param level Minimum level to keep.
void log_set_level(log_level level);

/// Current minimum level.
/// @return The current minimum level.
log_level log_get_level();

/// True if a log line at `level` would be written.
/// @param level Level to check.
/// @return `true` if a log line at that level would be written.
bool log_enabled(log_level level);

/// Replace the log destination. Pass nullptr to go back to the default (stderr).
/// @param sink Function that receives the log, or nullptr.
/// @param user Arbitrary pointer, passed back to `sink`.
void log_set_sink(log_sink sink, void *user = nullptr);

/// Name of a level, for example "WARN".
/// @param level Level to get the name of.
/// @return A static string, no need to free it.
const char *log_level_name(log_level level);

/// Write a log line using a printf format.
///
/// Prefer the NJIN_* macros below since they fill in `file` and `line`.
/// @param level Level of the log line.
/// @param file Source file name, or nullptr.
/// @param line Line number, or 0.
/// @param fmt printf-style format string.
void log_write(log_level level, const char *file, i32 line, const char *fmt,
               ...) NJIN_PRINTF(4, 5);
/// @}
} // namespace njin

/// @addtogroup grp_log
/// @{

/// Write a log line at `level`, filling in file and line. The macros below are shorter.
#define NJIN_LOG(level, ...)                                                   \
  ::njin::log_write((level), __FILE__, __LINE__, __VA_ARGS__)

/// Write a trace-level log line. Arguments are like printf.
#define NJIN_TRACE(...) NJIN_LOG(::njin::log_trace, __VA_ARGS__)
/// Write a debug-level log line. Arguments are like printf.
#define NJIN_DEBUG(...) NJIN_LOG(::njin::log_debug, __VA_ARGS__)
/// Write an info-level log line. Arguments are like printf.
#define NJIN_INFO(...) NJIN_LOG(::njin::log_info, __VA_ARGS__)
/// Write a warn-level log line. Arguments are like printf.
#define NJIN_WARN(...) NJIN_LOG(::njin::log_warn, __VA_ARGS__)
/// Write an error-level log line. Arguments are like printf.
#define NJIN_ERROR(...) NJIN_LOG(::njin::log_error, __VA_ARGS__)
/// Write a fatal-level log line, then stop the program with `std::abort()`.
#define NJIN_FATAL(...)                                                        \
  do {                                                                         \
    NJIN_LOG(::njin::log_fatal, __VA_ARGS__);                                  \
    std::abort();                                                              \
  } while (0)
/// @}
