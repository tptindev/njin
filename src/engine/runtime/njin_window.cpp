#include "njin_window.h"
#include "njin_ctx_impl.h"
#include "njin_file.h"
#include "njin_log.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <raylib.h>
#include <rlgl.h>

namespace njin {
void quit(context &ctx) { ctx.quit = true; }

void screenshot(context &ctx, const char *path) {
  if (path != nullptr) {
    ctx.screenshots.emplace_back(path);
    return;
  }
  const std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", std::localtime(&now));
  // Seconds alone collide when several are taken in one second: add the
  // milliseconds, then a counter if the same name is already queued.
  const std::string base = std::string("screenshots/screenshot_") + stamp +
                           "_" +
                           std::to_string((u64)(ctx.time.elapsed * 1000.0f) % 1000);
  std::string full = save_path(ctx, (base + ".png").c_str());
  for (i32 n = 2; std::find(ctx.screenshots.begin(), ctx.screenshots.end(),
                            full) != ctx.screenshots.end();
       n++)
    full = save_path(ctx, (base + "_" + std::to_string(n) + ".png").c_str());
  ctx.screenshots.push_back(full);
}

void take_pending_screenshots(context &ctx) {
  if (ctx.screenshots.empty())
    return;
  // Draw calls are batched: flush them, or the image misses the last ones.
  rlDrawRenderBatchActive();
  Image image = LoadImageFromScreen();
  for (const std::string &path : ctx.screenshots) {
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
  ctx.screenshots.clear();
}

void window_set_size(context &, vec2 size) {
  if (size.x >= 1.0f && size.y >= 1.0f && !IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE))
    SetWindowSize((int)size.x, (int)size.y);
}

void window_set_title(context &, const char *title) {
  if (title != nullptr)
    SetWindowTitle(title);
}

void window_set_fullscreen(context &ctx, bool fullscreen) {
  if (fullscreen != window_fullscreen(ctx))
    ToggleBorderlessWindowed();
}

bool window_fullscreen(const context &) {
  return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
}

void window_set_vsync(context &, bool vsync) {
  if (vsync)
    SetWindowState(FLAG_VSYNC_HINT);
  else
    ClearWindowState(FLAG_VSYNC_HINT);
}

bool window_vsync(const context &) { return IsWindowState(FLAG_VSYNC_HINT); }

bool window_resized(const context &) { return IsWindowResized(); }

void cursor_set_visible(context &, bool visible) {
  if (visible)
    ShowCursor();
  else
    HideCursor();
}

void cursor_set_locked(context &, bool locked) {
  if (locked)
    DisableCursor();
  else
    EnableCursor();
}
} // namespace njin
