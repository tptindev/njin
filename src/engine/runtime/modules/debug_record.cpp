#include "debug.h"
#include "njin_ctx_impl.h"
#include "njin_file.h"
#include "njin_log.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <raylib.h>
#include <rlgl.h>

namespace njin {
namespace {
// Averages the source pixels that fall in each destination pixel (a box filter): sharp enough for pixel art at
// 1/2 and 1/3, and no shimmer in motion, unlike picking every other pixel.
void shrink(const u8 *src, i32 sw, i32 sh, u8 *dst, i32 dw, i32 dh) {
  for (i32 y = 0; y < dh; y++) {
    const i32 y0 = (i32)((i64)y * sh / dh), y1 = std::max(y0 + 1, (i32)((i64)(y + 1) * sh / dh));
    for (i32 x = 0; x < dw; x++) {
      const i32 x0 = (i32)((i64)x * sw / dw), x1 = std::max(x0 + 1, (i32)((i64)(x + 1) * sw / dw));
      u32 r = 0, g = 0, b = 0;
      for (i32 sy = y0; sy < y1; sy++)
        for (i32 sx = x0; sx < x1; sx++) {
          const u8 *p = src + ((usize)sy * (usize)sw + (usize)sx) * 4;
          r += p[0];
          g += p[1];
          b += p[2];
        }
      const u32 n = (u32)((y1 - y0) * (x1 - x0));
      u8 *out = dst + ((usize)y * (usize)dw + (usize)x) * 4;
      out[0] = (u8)((r + n / 2) / n);
      out[1] = (u8)((g + n / 2) / n);
      out[2] = (u8)((b + n / 2) / n);
      out[3] = 255;
    }
  }
}

void finish(context &ctx, const char *why) {
  debug_recorder &r = ctx.debug.rec;
  if (!r.active)
    return;
  r.active = false;
  r.dirty = true;
  const bool ok = r.gif.close();
  if (!ok)
    r.error = "cannot write " + r.path;
  if (r.error.empty())
    NJIN_INFO("recording saved (%s): %s, %d frames, %.1f s, %.2f MB", why, r.path.c_str(), r.frames, r.elapsed,
              (f64)r.gif.bytes() / (1024.0 * 1024.0));
  else
    NJIN_WARN("recording failed: %s", r.error.c_str());
}
} // namespace

bool debug_record_start(context &ctx, f32 fps, f32 scale, f32 max_seconds) {
  debug_recorder &r = ctx.debug.rec;
  if (r.active)
    return true;
  r.dirty = true;
  r.error.clear();
  r.fps = std::clamp(fps, 1.0f, 50.0f);
  r.scale = std::clamp(scale, 0.1f, 1.0f);
  r.max_seconds = std::clamp(max_seconds, 1.0f, 600.0f);
  const i32 sw = GetRenderWidth(), sh = GetRenderHeight();
  if (sw < 1 || sh < 1) {
    r.error = "no window to record";
    return false;
  }
  r.width = std::max(1, (i32)std::lround((f32)sw * r.scale));
  r.height = std::max(1, (i32)std::lround((f32)sh * r.scale));

  const std::time_t now = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", std::localtime(&now));
  r.path = save_path(ctx, (std::string("recordings/rec_") + stamp + ".gif").c_str());
  std::error_code ec;
  const std::filesystem::path p(reinterpret_cast<const char8_t *>(r.path.c_str()));
  if (p.has_parent_path())
    std::filesystem::create_directories(p.parent_path(), ec);
  if (!r.gif.open(r.path, r.width, r.height)) {
    r.error = "cannot create " + r.path;
    NJIN_WARN("recording: %s", r.error.c_str());
    return false;
  }
  r.active = true;
  r.since_last = 1.0f / r.fps; // grab the first frame right away
  r.elapsed = 0.0f;
  r.owed_cs = 0.0f;
  r.frames = 0;
  NJIN_INFO("recording started: %s (%dx%d, %.0f fps)", r.path.c_str(), r.width, r.height, r.fps);
  return true;
}

void debug_record_stop(context &ctx) { finish(ctx, "stopped"); }

void debug_record_frame(context &ctx) {
  debug_recorder &r = ctx.debug.rec;
  if (!r.active)
    return;
  const f32 dt = ctx.time.dt_real;
  r.since_last += dt;
  if (r.since_last < 1.0f / r.fps - 0.0005f)
    return;
  // The window may have been resized: a GIF has one size, so scale the new frame to it.
  rlDrawRenderBatchActive(); // draw calls are batched: flush them, or the frame misses the last ones
  Image image = LoadImageFromScreen();
  if (image.data == nullptr) {
    r.error = "cannot read the screen";
    finish(ctx, "error");
    return;
  }
  if (image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  r.shrunk.resize((usize)r.width * (usize)r.height * 4);
  shrink(static_cast<const u8 *>(image.data), image.width, image.height, r.shrunk.data(), r.width, r.height);
  UnloadImage(image);

  // The time since the previous frame, in hundredths of a second, carrying the rounding so the animation
  // lasts as long as the recording did.
  r.owed_cs += r.since_last * 100.0f;
  const i32 delay = std::max(1, (i32)r.owed_cs);
  r.owed_cs -= (f32)delay;
  r.elapsed += r.since_last;
  r.since_last = 0.0f;
  if (!r.gif.add_frame(r.shrunk.data(), delay) || r.gif.failed()) {
    r.error = "cannot write " + r.path;
    finish(ctx, "error");
    return;
  }
  r.frames++;
  if (r.elapsed >= r.max_seconds)
    finish(ctx, "time limit");
}

json_value debug_record_status(const context &ctx) {
  const debug_recorder &r = ctx.debug.rec;
  json_value m = json_value::make_object();
  m.set("t", "rec").set("on", r.active).set("frames", r.frames).set("secs", r.elapsed).set("bytes", (i64)r.gif.bytes());
  m.set("w", r.width).set("h", r.height).set("fps", r.fps).set("max", r.max_seconds).set("file", r.path).set("err", r.error);
  std::string dir;
  if (!r.path.empty()) {
    const std::filesystem::path p(reinterpret_cast<const char8_t *>(r.path.c_str()));
    const auto u8 = p.parent_path().u8string();
    dir.assign(u8.begin(), u8.end());
  }
  m.set("dir", dir);
  return m;
}
} // namespace njin
