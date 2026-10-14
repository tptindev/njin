#pragma once
#include "njin_internal_only.h"

#include "njin_log.h"
#include <chrono>

namespace njin {
// Routes raylib's TraceLog output through the njin logger. Call before
// InitWindow so window/GL startup messages are captured too.
void log_capture_raylib();

// A second receiver for every message that passes the level filter, called
// before the sink. Used by the debug server to forward the log to the
// inspector without replacing the game's own sink. Null removes it.
void log_set_tap(log_sink tap, void *user);

// While held and while there is no tap, every message that passes the level
// filter is also kept (it is still printed as usual). Setting a tap replays
// them to it. Used from create so that the window's own startup messages
// can reach the inspector, which only starts listening a moment later. Turning
// it off drops what was kept.
void log_hold(bool on);

// Whether the default sink writes to stderr. Off while an inspector is
// connected, since the log is on its screen then; a sink installed with
// log_set_sink() is not affected.
void log_set_console(bool on);
} // namespace njin

// NJIN_WARN for a misuse a game may repeat every frame (too many lights,
// begin_3d twice): written the first time, then at most once every five
// seconds from the same line, so the log is not one line per frame.
#define NJIN_WARN_LIMITED(...)                                                                       do {                                                                                                 using njin_clock_ = std::chrono::steady_clock;                                                     static njin_clock_::time_point njin_next_{};                                                       const njin_clock_::time_point njin_now_ = njin_clock_::now();                                      if (njin_now_ >= njin_next_) {                                                                       njin_next_ = njin_now_ + std::chrono::seconds(5);                                                  NJIN_WARN(__VA_ARGS__);                                                                          }                                                                                                } while (0)
