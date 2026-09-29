#include "weather.h"
#include "levels.h"

#include <cmath>
#include <iterator>

namespace sandtable {

namespace {

constexpr f32 cloud_cell = 16.0f; // world units: clouds are drawn in blocks of this

u32 hash(u32 a, u32 b) {
  u32 h = (a * 0x9E3779B1u) ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  return h;
}
f32 unit(u32 a, u32 b) { return static_cast<f32>(hash(a, b) & 0xFFFFu) / 65535.0f; }

rgba faded(rgba c, f32 k) { return {c.r, c.g, c.b, c.a * k}; }

f32 wrap(f32 v, f32 lo, f32 span) { return lo + std::fmod(std::fmod(v - lo, span) + span, span); }

// Cloud `i`: a few puffs round a centre that drifts with the wind and comes
// back in on the other side.
struct cloud {
  vec2 center;
  vec2 puff[6];
  f32 radius[6];
  f32 size;
};

cloud cloud_at(const level_def &l, i32 i, f32 time) {
  const u32 seed = l.seed * 101u + static_cast<u32>(i);
  cloud c{};
  c.size = 90.0f + unit(seed, 1) * 110.0f;
  const f32 margin = 300.0f;
  const vec2 start{unit(seed, 2) * world_width, unit(seed, 3) * world_height};
  const vec2 moved = start + l.wind * time * (0.8f + 0.4f * unit(seed, 4));
  c.center = {wrap(moved.x, -margin, world_width + 2.0f * margin),
              wrap(moved.y, -margin, world_height + 2.0f * margin)};
  for (i32 k = 0; k < 6; ++k) {
    const f32 a = unit(seed, 10 + static_cast<u32>(k)) * 360.0f;
    const f32 d = unit(seed, 20 + static_cast<u32>(k)) * c.size * 0.6f;
    c.puff[k] = from_angle(a) * d * vec2{1.6f, 0.8f}; // wider than tall
    c.radius[k] = c.size * (0.35f + 0.3f * unit(seed, 30 + static_cast<u32>(k)));
  }
  return c;
}

bool in_cloud(const cloud &c, vec2 p) {
  for (i32 k = 0; k < 6; ++k)
    if (length_sq(p - (c.center + c.puff[k])) <= c.radius[k] * c.radius[k])
      return true;
  return false;
}

// Every block of the grid that the cloud covers, once, so its alpha does not
// pile up where the puffs overlap.
template <typename Fn> void for_cloud_cells(const cloud &c, Fn fn) {
  const f32 reach = c.size * 1.3f;
  const i32 x0 = static_cast<i32>(std::floor((c.center.x - reach * 1.6f) / cloud_cell));
  const i32 x1 = static_cast<i32>(std::floor((c.center.x + reach * 1.6f) / cloud_cell));
  const i32 y0 = static_cast<i32>(std::floor((c.center.y - reach) / cloud_cell));
  const i32 y1 = static_cast<i32>(std::floor((c.center.y + reach) / cloud_cell));
  for (i32 y = y0; y <= y1; ++y)
    for (i32 x = x0; x <= x1; ++x) {
      const vec2 p{(static_cast<f32>(x) + 0.5f) * cloud_cell, (static_cast<f32>(y) + 0.5f) * cloud_cell};
      if (in_cloud(c, p))
        fn(vec2{static_cast<f32>(x) * cloud_cell, static_cast<f32>(y) * cloud_cell});
    }
}

} // namespace

void draw_clouds(context &ctx) {
  const level_def &l = current_level();
  if (l.clouds <= 0.0f)
    return;
  const i32 count = static_cast<i32>(std::round(l.clouds * 14.0f));
  const f32 t = elapsed(ctx);
  // From afar the clouds are seen; close up the camera is under them and only
  // their shadows pass over the ground.
  const f32 zoom = camera_zoom();
  const f32 body = zoom <= 0.25f ? 0.3f : zoom <= 0.5f ? 0.15f : 0.0f;
  const vec2 shadow_off{40.0f, 70.0f};
  const rgba shadow = rgb(20, 30, 40, static_cast<i32>(40 + 40 * l.rain));
  const rgba white = l.rain > 0.3f ? rgb(170, 176, 186) : rgb(242, 244, 248);
  const rect table{{0.0f, 0.0f}, {world_width, world_height}};
  for (i32 i = 0; i < count; ++i) {
    const cloud c = cloud_at(l, i, t);
    for_cloud_cells(c, [&](vec2 at) {
      const vec2 s = at + shadow_off;
      if (point_in_rect(s, table))
        draw_rect(ctx, {s, {cloud_cell, cloud_cell}}, shadow);
    });
    if (body > 0.0f)
      for_cloud_cells(c, [&](vec2 at) {
        draw_rect(ctx, {at, {cloud_cell, cloud_cell}}, rgba{white.r, white.g, white.b, body});
      });
  }
}

void draw_rain(context &ctx) {
  const level_def &l = current_level();
  if (l.rain <= 0.0f)
    return;
  const vec2 scr = screen_size(ctx);
  const f32 t = elapsed(ctx);
  // A wet, grey day.
  draw_rect(ctx, {{0.0f, 0.0f}, scr}, rgb(30, 40, 60, static_cast<i32>(70 * l.rain)));
  // Streaks falling with the wind, each a few pixels, at a steady place in
  // the loop of its own speed.
  const i32 drops = static_cast<i32>(420.0f * l.rain);
  const f32 slant = l.wind.x * 0.01f;
  const rgba streak = rgb(190, 205, 225, 170);
  for (i32 i = 0; i < drops; ++i) {
    const f32 speed = 260.0f + unit(static_cast<u32>(i), 1) * 140.0f;
    const f32 y = wrap(unit(static_cast<u32>(i), 2) * scr.y + t * speed, -8.0f, scr.y + 16.0f);
    const f32 x = wrap(unit(static_cast<u32>(i), 3) * scr.x + (t * speed) * slant, 0.0f, scr.x);
    const vec2 head{std::floor(x), std::floor(y)};
    for (i32 k = 0; k < 4; ++k)
      draw_rect(ctx, {head - vec2{std::floor(slant * static_cast<f32>(k)), static_cast<f32>(k)}, {1.0f, 1.0f}},
                faded(streak, 1.0f - 0.2f * static_cast<f32>(k)));
  }
  // Splashes: short-lived pixels where drops hit, new ones every tenth of a second.
  const u32 tick = static_cast<u32>(t * 10.0f);
  const i32 splashes = static_cast<i32>(90.0f * l.rain);
  for (i32 i = 0; i < splashes; ++i) {
    const vec2 at{std::floor(unit(tick, static_cast<u32>(i) * 2u) * scr.x),
                  std::floor(unit(tick, static_cast<u32>(i) * 2u + 1u) * scr.y)};
    draw_rect(ctx, {at, {1.0f, 1.0f}}, rgb(220, 232, 245, 200));
    draw_rect(ctx, {at + vec2{-1.0f, 1.0f}, {1.0f, 1.0f}}, rgb(220, 232, 245, 120));
    draw_rect(ctx, {at + vec2{1.0f, 1.0f}, {1.0f, 1.0f}}, rgb(220, 232, 245, 120));
  }
}

f32 rain_reach() { return 1.0f - 0.2f * current_level().rain; }

vec3 daylight() {
  struct key {
    f32 hour;
    vec3 light;
  };
  static constexpr key keys[] = {
      {0.0f, {0.30f, 0.34f, 0.52f}},  {4.5f, {0.30f, 0.34f, 0.52f}}, {6.0f, {0.88f, 0.62f, 0.52f}},
      {8.0f, {1.0f, 1.0f, 1.0f}},     {17.0f, {1.0f, 1.0f, 1.0f}},   {18.5f, {0.98f, 0.64f, 0.46f}},
      {20.0f, {0.40f, 0.40f, 0.60f}}, {24.0f, {0.30f, 0.34f, 0.52f}},
  };
  const f32 h = state.hour;
  vec3 out = keys[0].light;
  for (usize i = 0; i + 1 < std::size(keys); ++i) {
    if (h >= keys[i].hour && h <= keys[i + 1].hour) {
      const f32 k = (h - keys[i].hour) / (keys[i + 1].hour - keys[i].hour);
      out = keys[i].light + (keys[i + 1].light - keys[i].light) * k;
      break;
    }
  }
  return out * (1.0f - 0.25f * current_level().rain);
}

f32 darkness() {
  const vec3 l = daylight();
  const f32 lum = l.x * 0.3f + l.y * 0.59f + l.z * 0.11f;
  return clamp((0.9f - lum) / 0.55f, 0.0f, 1.0f);
}

} // namespace sandtable
