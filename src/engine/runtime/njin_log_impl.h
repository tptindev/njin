#pragma once

namespace njin {
// Routes raylib's TraceLog output through the njin logger. Call before
// InitWindow so window/GL startup messages are captured too.
void log_capture_raylib();

// A second receiver for every message that passes the level filter, called
// before the sink. Used by the debug server to forward the log to the
// inspector without replacing the game's own sink. Null removes it.
void log_set_tap(log_sink tap, void *user);
} // namespace njin
