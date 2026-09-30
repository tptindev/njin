#include "weather.h"

#include <iterator>

namespace sandtable {

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
  return out;
}

f32 darkness() {
  const vec3 l = daylight();
  const f32 lum = l.x * 0.3f + l.y * 0.59f + l.z * 0.11f;
  return clamp((0.9f - lum) / 0.55f, 0.0f, 1.0f);
}

} // namespace sandtable
