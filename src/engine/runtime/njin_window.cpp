#include "njin_image_io.h"
#include "njin_window.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_file.h"
#include "njin_log.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include "njin_rl.h"
#include <rlgl.h>
#include <stdexcept>

namespace njin {
void quit(context &ctx) { ctx_ref<frame_state>(ctx).quit = true; }

void window_set_close_intercept(context &ctx, bool on) { ctx_ref<frame_state>(ctx).close_intercept = on; }

bool window_close_requested(const context &ctx) { return ctx_ref<frame_state>(ctx).close_requested; }

void screenshot(context &ctx, const char *path) {
  if (path != nullptr) {
    ctx_ref<frame_state>(ctx).screenshots.emplace_back(path);
    return;
  }
  const std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", std::localtime(&now));
  // Seconds alone collide when several are taken in one second: add the
  // milliseconds, then a counter if the same name is already queued.
  const std::string base = std::string("screenshots/screenshot_") + stamp +
                           "_" +
                           std::to_string((u64)(ctx_ref<time_state>(ctx).elapsed * 1000.0f) % 1000);
  std::string full = save_path(ctx, (base + ".png").c_str());
  for (i32 n = 2; std::find(ctx_ref<frame_state>(ctx).screenshots.begin(), ctx_ref<frame_state>(ctx).screenshots.end(),
                            full) != ctx_ref<frame_state>(ctx).screenshots.end();
       n++)
    full = save_path(ctx, (base + "_" + std::to_string(n) + ".png").c_str());
  ctx_ref<frame_state>(ctx).screenshots.push_back(full);
}

void take_pending_screenshots(context &ctx) {
  if (ctx_ref<frame_state>(ctx).screenshots.empty())
    return;
  // Draw calls are batched: flush them, or the image misses the last ones.
  rlDrawRenderBatchActive();
  Image image = LoadImageFromScreen();
  for (const std::string &path : ctx_ref<frame_state>(ctx).screenshots) {
    std::error_code ec;
    const std::filesystem::path p(reinterpret_cast<const char8_t *>(path.c_str()));
    if (p.has_parent_path())
      std::filesystem::create_directories(p.parent_path(), ec);
    if (ExportImage(image, path.c_str()))
      NJIN_INFO("screenshot saved: %s", path.c_str());
    else
      NJIN_WARN("screenshot: cannot save %s (unknown extension or unwritable)",
                path.c_str());
  }
  UnloadImage(image);
  ctx_ref<frame_state>(ctx).screenshots.clear();
}
void save_pending_capture(context &ctx,const RenderImageAsset &capture) {
  if(capture.pixels.empty()) return;
  for(const auto &path:ctx_ref<frame_state>(ctx).screenshots) {
    const std::filesystem::path output(reinterpret_cast<const char8_t*>(path.c_str()));
    std::error_code error;
    if(output.has_parent_path()) std::filesystem::create_directories(output.parent_path(),error);
    if(error || !image_io_save(path.c_str(),capture.width,capture.height,capture.pixels)) throw std::runtime_error("njin capture: cannot export "+path);
  }
  ctx_ref<frame_state>(ctx).screenshots.clear();
}

void window_set_size([[maybe_unused]] context &ctx, vec2 size) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) {
    if(size.x>=1 && size.y>=1) gpu->set_size((int)size.x,(int)size.y);
    return;
  }
#endif
  if (size.x >= 1.0f && size.y >= 1.0f && !IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE))
    SetWindowSize((int)size.x, (int)size.y);
}

vec2 monitor_size([[maybe_unused]] const context &ctx) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { return gpu->monitor_bounds().size; }
#endif

  const int m = GetCurrentMonitor();
  return {(f32)GetMonitorWidth(m), (f32)GetMonitorHeight(m)};
}

void window_center([[maybe_unused]] context &ctx) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { gpu->center(); return; }
#endif

  if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) || IsWindowFullscreen())
    return;
  const int m = GetCurrentMonitor();
  const Vector2 at = GetMonitorPosition(m);
  const int x = (int)at.x + std::max(0, (GetMonitorWidth(m) - GetScreenWidth()) / 2);
  const int y = (int)at.y + std::max(0, (GetMonitorHeight(m) - GetScreenHeight()) / 2);
  SetWindowPosition(x, y);
}

void window_set_position([[maybe_unused]] context &ctx, vec2 pos) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { gpu->set_position(pos); return; }
#endif

  if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) || IsWindowFullscreen())
    return;
  SetWindowPosition((int)pos.x, (int)pos.y);
}

vec2 window_position([[maybe_unused]] const context &ctx) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { return gpu->position(); }
#endif

  const Vector2 at = GetWindowPosition();
  return {at.x, at.y};
}

vec2 monitor_position([[maybe_unused]] const context &ctx) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { return gpu->monitor_bounds().pos; }
#endif

  const Vector2 at = GetMonitorPosition(GetCurrentMonitor());
  return {at.x, at.y};
}

void *window_native_handle([[maybe_unused]] const context &ctx) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { return gpu->native_handle(); }
#endif
 return GetWindowHandle(); }

void window_set_title([[maybe_unused]] context &ctx, const char *title) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { gpu->set_title(title); return; }
#endif

  if (title != nullptr)
    SetWindowTitle(title);
}

void window_set_fullscreen(context &ctx, bool fullscreen) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { gpu->set_fullscreen(fullscreen); return; }
#endif

  if (fullscreen == window_fullscreen(ctx))
    return;
  ToggleBorderlessWindowed();
  // Out of it raylib puts the title bar back: not on a window made without one.
  if (!fullscreen && !ctx_ref<config>(ctx).decorated)
    SetWindowState(FLAG_WINDOW_UNDECORATED);
}

bool window_fullscreen(const context &ctx) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { return gpu->fullscreen(); }
#endif

  return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
}

void window_set_vsync([[maybe_unused]] context &ctx, bool vsync) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { gpu->set_vsync(vsync); return; }
#endif

  if (vsync)
    SetWindowState(FLAG_VSYNC_HINT);
  else
    ClearWindowState(FLAG_VSYNC_HINT);
}

bool window_vsync([[maybe_unused]] const context &ctx) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { return gpu->vsync(); }
#endif
 return IsWindowState(FLAG_VSYNC_HINT); }

bool window_resized([[maybe_unused]] const context &ctx) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { return gpu->resized(); }
#endif
 return IsWindowResized(); }

void cursor_set_visible([[maybe_unused]] context &ctx, bool visible) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { gpu->show_cursor(visible); return; }
#endif

  if (visible)
    ShowCursor();
  else
    HideCursor();
}

void cursor_set_locked([[maybe_unused]] context &ctx, bool locked) {
#if NJIN_BGFX
  if(auto *gpu=ctx_ref<window_guard>(ctx).platform.get()) { gpu->lock_cursor(locked); return; }
#endif

  if (locked)
    DisableCursor();
  else
    EnableCursor();
}
} // namespace njin
