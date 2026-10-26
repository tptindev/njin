#pragma once
#include "njin_internal_only.h"

#include "_math.h"
#include "_types.h"
#include "njin_input.h"
#include <vector>

// Touch screens (touch_count, touch_get, gestures in njin_input.h).
//
// GLFW has no multi-touch, so on Windows the runtime listens to the window's
// WM_POINTER messages itself (njin_touch_win32.cpp, apart from raylib.h) and
// passes every message on: Windows still turns the first finger into mouse
// input for GLFW, so mouse-only code keeps working with a finger. Elsewhere
// there is no touch input yet.
//
// The raw events of a frame are folded into the tracked fingers once, at the
// top of the frame, right after the mouse; positions go through the virtual
// screen like the mouse's.
namespace njin {
struct view_state;

// One event from the window, in client pixels.
struct touch_raw {
  enum kind : u8 { down, move, up };
  i64 os_id = 0;
  f32 x = 0.0f, y = 0.0f;
  kind what = move;
};

// Starts listening to the window's touch messages (`window` is the native
// handle). False where touch is not read. Safe to call again for a new window.
bool touch_native_install(void *window);
// The events since the last call, oldest first.
void touch_native_take(std::vector<touch_raw> &out);

struct touch_track {
  i64 os_id = 0;
  touch_point pub{};    // what touch_get() answers (position in screen units)
  vec2 prev{};          // position last frame
  vec2 raw{}, raw_start{}; // client pixels, for the tap slop
  f64 since = 0.0;      // real time it went down
  bool down = true;
  bool moved = false;   // went past the tap slop
  bool long_sent = false;
};

struct touch_state {
  bool tried = false; // touch_native_install ran
  std::vector<touch_track> points;
  std::vector<touch_raw> events; // scratch
  gesture_state gestures{};
  i32 next_id = 1;
};

// Folds the window's events into `touch` and works out the gestures. `window`
// is the native window handle, for the first call.
void touch_poll(touch_state &touch, const view_state &view, void *window, f64 now);
} // namespace njin
