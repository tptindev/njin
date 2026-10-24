#include "njin_pace.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h> // timeBeginPeriod, timeEndPeriod
#else
#include <chrono>
#include <thread>
#endif

namespace njin {

#if defined(_WIN32)
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

void pacer_open(frame_pacer &p) {
  // Windows 10 1803 and later: precise to well under a millisecond, without
  // raising the timer resolution of the whole system (which costs every
  // process power: the scheduler wakes a thousand times a second).
  p.timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  p.precise = p.timer != nullptr;
  if (p.precise)
    timeEndPeriod(1); // raylib's InitTimer asked for 1 ms; Sleep() is not used here
}

void pacer_close(frame_pacer &p) {
  if (p.timer != nullptr) {
    CloseHandle((HANDLE)p.timer);
    // raylib's CloseWindow ends the 1 ms period it began: begin it again so
    // the two stay paired.
    timeBeginPeriod(1);
  }
  p.timer = nullptr;
  p.precise = false;
}

void pacer_sleep(frame_pacer &p, double seconds) {
  if (seconds <= 0.0)
    return;
  if (p.timer != nullptr) {
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)(seconds * 1e7); // relative, in 100 ns units
    if (SetWaitableTimerEx((HANDLE)p.timer, &due, 0, nullptr, nullptr, nullptr, 0)) {
      WaitForSingleObject((HANDLE)p.timer, INFINITE);
      return;
    }
  }
  Sleep((DWORD)(seconds * 1000.0)); // raylib's 1 ms period still holds here
}
#else
void pacer_open(frame_pacer &p) { p.precise = true; }
void pacer_close(frame_pacer &p) { p.precise = false; }
void pacer_sleep(frame_pacer &, double seconds) {
  if (seconds > 0.0)
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}
#endif

} // namespace njin
