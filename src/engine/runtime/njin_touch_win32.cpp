// Kept apart from the rest of the runtime: <windows.h> and <raylib.h> declare
// clashing names (CloseWindow, Rectangle, ...) and cannot share a file.
#include "njin_touch.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// WM_POINTER and GetPointerInfo: Windows 8.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#endif

namespace njin {
#if defined(_WIN32)
namespace {
// One window per process: the one raylib opened.
HWND hooked = nullptr;
WNDPROC next_proc = nullptr;
std::vector<touch_raw> pending;

LRESULT CALLBACK touch_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_POINTERDOWN || msg == WM_POINTERUPDATE || msg == WM_POINTERUP ||
      msg == WM_POINTERCAPTURECHANGED) {
    const UINT32 id = GET_POINTERID_WPARAM(wparam);
    POINTER_INPUT_TYPE type = PT_POINTER;
    POINTER_INFO info{};
    if (GetPointerType(id, &type) && type == PT_TOUCH && GetPointerInfo(id, &info)) {
      POINT p = info.ptPixelLocation;
      ScreenToClient(window, &p);
      touch_raw e;
      e.os_id = (i64)id;
      e.x = (f32)p.x;
      e.y = (f32)p.y;
      // Lost capture ends the touch where it was.
      e.what = msg == WM_POINTERDOWN ? touch_raw::down
               : (msg == WM_POINTERUP || msg == WM_POINTERCAPTURECHANGED) ? touch_raw::up
                                                                          : touch_raw::move;
      if (pending.size() < 4096)
        pending.push_back(e);
    }
  }
  // GLFW handles the rest; the pointer messages it leaves to Windows, which
  // turns the first finger into mouse input.
  return CallWindowProcW(next_proc, window, msg, wparam, lparam);
}
} // namespace

bool touch_native_install(void *window) {
  HWND h = (HWND)window;
  if (h == nullptr || h == hooked)
    return h != nullptr;
  pending.clear();
  next_proc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)&touch_proc);
  hooked = next_proc != nullptr ? h : nullptr;
  return hooked != nullptr;
}

void touch_native_take(std::vector<touch_raw> &out) {
  out.swap(pending);
  pending.clear();
}
#else
bool touch_native_install(void *) { return false; }
void touch_native_take(std::vector<touch_raw> &out) { out.clear(); }
#endif
} // namespace njin
