#pragma once
#include "njin_internal_only.h"

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
// them to it. Used from njin_create so that the window's own startup messages
// can reach the inspector, which only starts listening a moment later. Turning
// it off drops what was kept.
void log_hold(bool on);

// Whether the default sink writes to stderr. Off while an inspector is
// connected, since the log is on its screen then; a sink installed with
// log_set_sink() is not affected.
void log_set_console(bool on);
} // namespace njin
