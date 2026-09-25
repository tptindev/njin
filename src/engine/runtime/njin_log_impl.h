#pragma once

namespace njin {
// Routes raylib's TraceLog output through the njin logger. Call before
// InitWindow so window/GL startup messages are captured too.
void log_capture_raylib();
} // namespace njin
