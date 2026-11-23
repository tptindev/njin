#include "njin_ctx.h"
#include "njin_log.h"
#include "njin_world3d_impl.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

// Temporary marks on a terrain (terrain3d_imprint_*, njin_world3d.h): the CPU
// side. Stamps write the texels they cover; the texels' stamp times let the
// terrain shader (modules/world3d_draw.cpp) fade them, so nothing runs while
// marks fade. Only the changed rectangles go to the GPU (imprint_upload).

namespace njin {
namespace {
u16 tick_of(f32 seconds) { return (u16)((u64)std::max(seconds * 100.0f, 0.0f) & 0xFFFFu); }

i32 wrap(i32 v, i32 n) { return ((v % n) + n) % n; }

u8 *texel(terrain_imprint &im, i32 x, i32 z) {
  const i32 n = im.desc.size;
  return &im.texels[((usize)wrap(z, n) * (usize)n + (usize)wrap(x, n)) * 4];
}

// How much of a mark stamped at `tick` is left at `now` (the shader's own rule).
f32 left(const terrain3d_imprint_desc &d, u16 tick, u16 now) {
  const f32 age = (f32)(u16)(now - tick) * 0.01f;
  return std::clamp((d.lifetime - age) / std::max(d.fade, 1e-3f), 0.0f, 1.0f);
}

// Zeroes the texels of [x0, x1) x [z0, z1) that `keep` does not want, and queues the upload.
template <typename Keep> void clear(terrain_imprint &im, i32 x0, i32 z0, i32 x1, i32 z1, Keep keep) {
  for (i32 z = z0; z < z1; z++)
    for (i32 x = x0; x < x1; x++) {
      u8 *t = texel(im, x, z);
      if (!keep(t))
        std::memset(t, 0, 4);
    }
  im.dirty.push_back({x0, z0, x1, z1, 0.0f});
}

void clear_all(terrain_imprint &im) {
  std::fill(im.texels.begin(), im.texels.end(), (u8)0);
  im.marks.clear();
  im.dirty.clear();
  im.dirty.push_back({im.x0, im.z0, im.x0 + im.desc.size, im.z0 + im.desc.size, 0.0f});
}

f32 sample(const terrain_imprint::shape &s, f32 u, f32 v, i32 channel) {
  // u across 0..1, v from the tip (row 0) 0..1, bilinear.
  const f32 fx = std::clamp(u, 0.0f, 1.0f) * (f32)(s.w - 1), fy = std::clamp(v, 0.0f, 1.0f) * (f32)(s.h - 1);
  const i32 x = std::min((i32)fx, s.w - 2 < 0 ? 0 : s.w - 2), y = std::min((i32)fy, s.h - 2 < 0 ? 0 : s.h - 2);
  const f32 ax = fx - (f32)x, ay = fy - (f32)y;
  const auto at = [&](i32 cx, i32 cy) {
    cx = std::min(cx, s.w - 1), cy = std::min(cy, s.h - 1);
    return (f32)s.pressed_rim[((usize)cy * (usize)s.w + (usize)cx) * 2 + (usize)channel] / 255.0f;
  };
  return (at(x, y) * (1 - ax) + at(x + 1, y) * ax) * (1 - ay) + (at(x, y + 1) * (1 - ax) + at(x + 1, y + 1) * ax) * ay;
}
} // namespace

void terrain3d_imprint_set(context &ctx, terrain3d_handle handle, const terrain3d_imprint_desc &desc) {
  terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr)
    return;
  terrain_imprint &im = t->imprint;
  terrain_imprint next;
  if (desc.size > 0) {
    if (!std::isfinite(desc.texel) || desc.texel <= 0 || !std::isfinite(desc.depth) || desc.depth < 0 ||
        !std::isfinite(desc.rim) || desc.rim < 0 || !std::isfinite(desc.lifetime) || desc.lifetime < 0 ||
        !std::isfinite(desc.fade) || desc.fade < 0)
      throw std::invalid_argument("njin terrain imprint: invalid scale, relief or fade");
    for (float value : {desc.tint.r, desc.tint.g, desc.tint.b, desc.tint.a})
      if (!std::isfinite(value) || value < 0 || value > 1)
        throw std::invalid_argument("njin terrain imprint: tint must be finite in [0,1]");
    next.desc = desc;
    next.desc.size = std::clamp(desc.size, 64, 4096);
    next.desc.texel = std::max(desc.texel, 1e-3f);
    next.texels.assign((usize)next.desc.size * (usize)next.desc.size * 4, 0);
  }
  if (im.portable_texture)
    renderer_destroy_texture(ctx, im.portable_texture);
  next.shapes = std::move(im.shapes);
  next.tex = im.tex; // world3d_draw.cpp remakes it when the size changes
  next.tex_size = im.tex_size;
  im = std::move(next);
}

void terrain3d_imprint_follow(context &ctx, terrain3d_handle handle, vec3 center) {
  terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr || t->imprint.desc.size <= 0)
    return;
  terrain_imprint &im = t->imprint;
  const i32 n = im.desc.size;
  const i32 x0 = (i32)std::floor(center.x / im.desc.texel) - n / 2;
  const i32 z0 = (i32)std::floor(center.z / im.desc.texel) - n / 2;
  if (!im.placed || std::abs(x0 - im.x0) >= n || std::abs(z0 - im.z0) >= n) {
    im.x0 = x0, im.z0 = z0, im.placed = true;
    clear_all(im);
  } else if (x0 != im.x0 || z0 != im.z0) {
    // The strips that come into the square hold what left it: clear them.
    const auto none = [](const u8 *) { return false; };
    if (x0 > im.x0)
      clear(im, im.x0 + n, z0, x0 + n, z0 + n, none);
    else if (x0 < im.x0)
      clear(im, x0, z0, im.x0, z0 + n, none);
    if (z0 > im.z0)
      clear(im, x0, im.z0 + n, x0 + n, z0 + n, none);
    else if (z0 < im.z0)
      clear(im, x0, z0, x0 + n, im.z0, none);
    im.x0 = x0, im.z0 = z0;
  }
  // Marks gone by now are cleared, so a texel the wrapping square brings back
  // later never shows an old one; texels stamped again since are kept.
  const f32 now = elapsed(ctx);
  const u16 tick = tick_of(now);
  while (!im.marks.empty() && now - im.marks.front().born > im.desc.lifetime) {
    const terrain_imprint::mark m = im.marks.front();
    im.marks.pop_front();
    const i32 cx0 = std::max(m.x0, im.x0), cz0 = std::max(m.z0, im.z0);
    const i32 cx1 = std::min(m.x1, im.x0 + n), cz1 = std::min(m.z1, im.z0 + n);
    if (cx0 < cx1 && cz0 < cz1)
      clear(im, cx0, cz0, cx1, cz1, [&](const u8 *v) { return left(im.desc, (u16)(v[2] | v[3] << 8), tick) > 0.0f; });
  }
}

i32 terrain3d_imprint_shape(context &ctx, terrain3d_handle handle, const u8 *pressed_rim, i32 width, i32 height) {
  terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr || pressed_rim == nullptr || width < 1 || height < 1 || width > 1024 || height > 1024) {
    NJIN_WARN("terrain3d_imprint_shape: no terrain, or no pixels, or a size outside 1..1024");
    return -1;
  }
  terrain_imprint::shape s{width, height, {}};
  s.pressed_rim.assign(pressed_rim, pressed_rim + (usize)width * (usize)height * 2);
  t->imprint.shapes.push_back(std::move(s));
  return (i32)t->imprint.shapes.size() - 1;
}

void terrain3d_imprint_stamp(context &ctx, terrain3d_handle handle, const terrain3d_stamp &stamp) {
  terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr || t->imprint.desc.size <= 0 || !t->imprint.placed)
    return;
  terrain_imprint &im = t->imprint;
  const terrain_imprint::shape *shape =
      stamp.shape >= 0 && (usize)stamp.shape < im.shapes.size() ? &im.shapes[(usize)stamp.shape] : nullptr;
  const f32 k = std::clamp(stamp.strength, 0.0f, 1.0f);
  const vec2 half{std::max(stamp.half_size.x, 1e-3f), std::max(stamp.half_size.y, 1e-3f)};
  if (k <= 0.0f || !std::isfinite(stamp.position.x) || !std::isfinite(stamp.position.z) || !std::isfinite(stamp.yaw))
    return;
  const f32 r = stamp.yaw * (3.14159265f / 180.0f);
  const vec2 tip{std::sin(r), std::cos(r)}, across{std::cos(r), -std::sin(r)};
  const f32 reach = std::sqrt(half.x * half.x + half.y * half.y);
  const f32 tx = im.desc.texel;
  const i32 n = im.desc.size;
  const i32 x0 = std::max((i32)std::floor((stamp.position.x - reach) / tx), im.x0);
  const i32 z0 = std::max((i32)std::floor((stamp.position.z - reach) / tx), im.z0);
  const i32 x1 = std::min((i32)std::ceil((stamp.position.x + reach) / tx) + 1, im.x0 + n);
  const i32 z1 = std::min((i32)std::ceil((stamp.position.z + reach) / tx) + 1, im.z0 + n);
  if (x0 >= x1 || z0 >= z1)
    return;
  const f32 now = elapsed(ctx);
  const u16 tick = tick_of(now);
  for (i32 z = z0; z < z1; z++)
    for (i32 x = x0; x < x1; x++) {
      const vec2 d{((f32)x + 0.5f) * tx - stamp.position.x, ((f32)z + 0.5f) * tx - stamp.position.z};
      const f32 u = (d.x * across.x + d.y * across.y) / half.x, v = (d.x * tip.x + d.y * tip.y) / half.y;
      if (std::fabs(u) > 1.0f || std::fabs(v) > 1.0f)
        continue;
      f32 press, rim;
      if (shape != nullptr) {
        press = sample(*shape, u * 0.5f + 0.5f, 0.5f - v * 0.5f, 0);
        rim = sample(*shape, u * 0.5f + 0.5f, 0.5f - v * 0.5f, 1);
      } else {
        const f32 e = std::sqrt(u * u + v * v);
        press = 1.0f - std::clamp((e - 0.55f) / 0.3f, 0.0f, 1.0f);
        rim = std::max(0.0f, 1.0f - std::fabs(e - 0.92f) / 0.08f);
      }
      press *= k, rim *= k;
      if (press <= 0.0f && rim <= 0.0f)
        continue;
      u8 *tv = texel(im, x, z);
      const f32 was = left(im.desc, (u16)(tv[2] | tv[3] << 8), tick);
      const f32 old_press = (f32)tv[0] / 255.0f * was, old_rim = (f32)tv[1] / 255.0f * was;
      // The new print presses down whatever rim stood where it lands.
      const f32 p = std::max(old_press, press), g = std::max(old_rim * (1.0f - press), rim);
      tv[0] = (u8)std::lround(p * 255.0f);
      tv[1] = (u8)std::lround(g * 255.0f);
      tv[2] = (u8)(tick & 0xFF);
      tv[3] = (u8)(tick >> 8);
    }
  im.marks.push_back({x0, z0, x1, z1, now});
  im.dirty.push_back({x0, z0, x1, z1, now});
}

void terrain3d_imprint_clear(context &ctx, terrain3d_handle handle) {
  terrain3d_slot *t = terrain_of(ctx, handle);
  if (t != nullptr && t->imprint.desc.size > 0)
    clear_all(t->imprint);
}

} // namespace njin
