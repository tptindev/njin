#include "njin_log.h"
#include "njin_log_impl.h"
#include <chrono>
#include <cstdarg>
#include <raylib.h>
#include <string>
#include <vector>

namespace njin {
namespace {
struct held_line {
  log_level level;
  std::string file;
  bool has_file;
  i32 line;
  std::string msg;
};

// The most lines log_hold keeps: window and audio startup is a few hundred.
constexpr usize max_held = 1000;

struct logger {
#ifdef NDEBUG
  log_level level = log_info;
#else
  log_level level = log_debug;
#endif
  log_sink sink = nullptr;
  void *user = nullptr;
  log_sink tap = nullptr;
  void *tap_user = nullptr;
  bool console = true;
  bool hold = false;
  std::vector<held_line> held;
};

logger &state() {
  static logger instance;
  return instance;
}

f64 seconds_since_start() {
  using clock = std::chrono::steady_clock;
  static const clock::time_point start = clock::now();
  return std::chrono::duration<f64>(clock::now() - start).count();
}

// "C:/a/b/file.cpp" -> "file.cpp"
const char *base_name(const char *path) {
  const char *name = path;
  for (const char *c = path; *c != '\0'; ++c) {
    if (*c == '/' || *c == '\\') {
      name = c + 1;
    }
  }
  return name;
}

void default_sink(log_level level, const char *file, i32 line, const char *msg,
                  void *) {
  const f64 t = seconds_since_start();
  if (file == nullptr) {
    std::fprintf(stderr, "[%8.3f] %-5s %s\n", t, log_level_name(level), msg);
  } else if (line <= 0) {
    std::fprintf(stderr, "[%8.3f] %-5s %s  %s\n", t, log_level_name(level),
                 file, msg);
  } else {
    std::fprintf(stderr, "[%8.3f] %-5s %s:%d  %s\n", t, log_level_name(level),
                 base_name(file), line, msg);
  }
}

void emit(log_level level, const char *file, i32 line, const char *fmt,
          va_list args) {
  char msg[1024];
  std::vsnprintf(msg, sizeof(msg), fmt, args);
  logger &log = state();
  if (log.tap != nullptr)
    log.tap(level, file, line, msg, log.tap_user);
  else if (log.hold && log.held.size() < max_held)
    log.held.push_back({level, file != nullptr ? file : "", file != nullptr, line, msg});
  if (log.sink != nullptr) {
    log.sink(level, file, line, msg, log.user);
  } else if (log.console) {
    default_sink(level, file, line, msg, nullptr);
  }
}

log_level from_raylib_level(i32 level) {
  switch (level) {
  case LOG_TRACE:
    return log_trace;
  case LOG_DEBUG:
    return log_debug;
  case LOG_INFO:
    return log_info;
  case LOG_WARNING:
    return log_warn;
  case LOG_ERROR:
    return log_error;
  case LOG_FATAL:
    return log_fatal;
  default:
    return log_info;
  }
}

void raylib_callback(int rl_level, const char *fmt, va_list args) {
  const log_level level = from_raylib_level(rl_level);
  if (log_enabled(level)) {
    // Tagged "njin", not "raylib": the engine seals raylib off from game
    // code (see src/engine/guard/), and the log should not say otherwise.
    emit(level, "njin", 0, fmt, args);
  }
}
} // namespace

void log_set_level(log_level level) { state().level = level; }
log_level log_get_level() { return state().level; }

bool log_enabled(log_level level) {
  return level != log_off && level >= state().level;
}

void log_set_sink(log_sink sink, void *user) {
  state().sink = sink;
  state().user = user;
}

const char *log_level_name(log_level level) {
  switch (level) {
  case log_trace:
    return "TRACE";
  case log_debug:
    return "DEBUG";
  case log_info:
    return "INFO";
  case log_warn:
    return "WARN";
  case log_error:
    return "ERROR";
  case log_fatal:
    return "FATAL";
  default:
    return "?";
  }
}

void log_write(log_level level, const char *file, i32 line, const char *fmt,
               ...) {
  if (!log_enabled(level) || fmt == nullptr) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  emit(level, file, line, fmt, args);
  va_end(args);
}

void log_capture_raylib() {
  seconds_since_start();
  // raylib filters before calling the callback; let everything through and
  // filter in log_enabled instead.
  SetTraceLogLevel(LOG_ALL);
  SetTraceLogCallback(raylib_callback);
}
void log_set_tap(log_sink tap, void *user) {
  logger &log = state();
  log.tap = tap;
  log.tap_user = user;
  if (tap == nullptr)
    return;
  log.hold = false;
  std::vector<held_line> held;
  held.swap(log.held);
  for (const held_line &h : held)
    tap(h.level, h.has_file ? h.file.c_str() : nullptr, h.line, h.msg.c_str(), user);
}

void log_hold(bool on) {
  state().hold = on;
  if (!on)
    state().held.clear();
}

void log_set_console(bool on) { state().console = on; }
} // namespace njin
