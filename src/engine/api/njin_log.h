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
// raylib's own messages go through the same logger, tagged "raylib".

#if defined(__MINGW_PRINTF_FORMAT)
#define NJIN_PRINTF(fmt, args) __attribute__((format(__MINGW_PRINTF_FORMAT, fmt, args)))
#elif defined(__GNUC__) || defined(__clang__)
#define NJIN_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define NJIN_PRINTF(fmt, args)
#endif

namespace njin {
enum log_level {
  log_trace,
  log_debug,
  log_info,
  log_warn,
  log_error,
  log_fatal,
  log_off
};

// Receives every message that passes the level filter. file is nullptr and
// line is 0 when the source is unknown (e.g. raylib).
using log_sink = void (*)(log_level level, const char *file, i32 line,
                          const char *msg, void *user);

// Messages below this level are dropped. Default: log_debug in debug builds,
// log_info with NDEBUG.
void log_set_level(log_level level);
log_level log_get_level();
bool log_enabled(log_level level);

// Replaces the output. nullptr restores the default (stderr).
void log_set_sink(log_sink sink, void *user = nullptr);

const char *log_level_name(log_level level);

// Prefer the NJIN_* macros below; they fill in file and line.
void log_write(log_level level, const char *file, i32 line, const char *fmt,
               ...) NJIN_PRINTF(4, 5);
} // namespace njin

#define NJIN_LOG(level, ...)                                                   \
  ::njin::log_write((level), __FILE__, __LINE__, __VA_ARGS__)

#define NJIN_TRACE(...) NJIN_LOG(::njin::log_trace, __VA_ARGS__)
#define NJIN_DEBUG(...) NJIN_LOG(::njin::log_debug, __VA_ARGS__)
#define NJIN_INFO(...) NJIN_LOG(::njin::log_info, __VA_ARGS__)
#define NJIN_WARN(...) NJIN_LOG(::njin::log_warn, __VA_ARGS__)
#define NJIN_ERROR(...) NJIN_LOG(::njin::log_error, __VA_ARGS__)
// Logs, then aborts the program.
#define NJIN_FATAL(...)                                                        \
  do {                                                                         \
    NJIN_LOG(::njin::log_fatal, __VA_ARGS__);                                  \
    std::abort();                                                              \
  } while (0)
