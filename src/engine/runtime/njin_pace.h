#pragma once
#include "njin_internal_only.h"

// Frame pacing for config::low_power: the thread sleeps the whole wait, with
// no busy loop at its end. Kept apart from raylib.h (windows.h clashes with it).
namespace njin {
struct frame_pacer {
  void *timer = nullptr; // Windows: a high-resolution waitable timer
  bool precise = false;  // sleeps land within a fraction of a millisecond
};

// Sets the pacer up. On Windows, with a high-resolution timer available, also
// gives back raylib's system-wide 1 ms timer period: nothing here needs it.
void pacer_open(frame_pacer &p);
void pacer_close(frame_pacer &p);
// Sleeps `seconds` (nothing when not positive).
void pacer_sleep(frame_pacer &p, double seconds);
} // namespace njin
