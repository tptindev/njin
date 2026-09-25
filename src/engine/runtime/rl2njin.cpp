#include "rl2njin.h"
#include <raylib.h>

namespace njin {

void from_raylib(Vector2 from, vec2 &to) {
  to.x = from.x;
  to.y = from.y;
}

void from_raylib(Vector4 from, vec4 &to) {
  to.x = from.x;
  to.y = from.y;
  to.z = from.z;
  to.w = from.w;
}

void from_raylib(Color from, vec4 &to) {
  to.x = from.r;
  to.y = from.g;
  to.z = from.b;
  to.w = from.a;
}
void from_raylib(Color from, rgba &to) {
  to.r = from.r / 255.0f;
  to.g = from.g / 255.0f;
  to.b = from.b / 255.0f;
  to.a = from.a / 255.0f;
}

void from_raylib(Camera2D from, camera_view &to) {
  to.zoom = from.zoom;
  to.rotation = from.rotation;
  from_raylib(from.target, to.target);
  from_raylib(from.offset, to.offset);
}

} // namespace njin
