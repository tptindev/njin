#include "rl2njin.h"
#include <raylib.h>

void njin::from_raylib(Vector2 from, vec2 &to) {
  to.x = from.x;
  to.y = from.y;
}

void njin::from_raylib(Vector4 from, vec4 &to) {
  to.x = from.x;
  to.y = from.y;
  to.z = from.z;
  to.w = from.w;
}

void njin::from_raylib(Color from, vec4 &to) {
  to.x = from.r;
  to.y = from.g;
  to.z = from.b;
  to.w = from.a;
}
void njin::from_raylib(Color from, rgba &to) {
  to.r = from.r;
  to.g = from.g;
  to.b = from.b;
  to.a = from.a;
}

void njin::from_raylib(Camera2D from, camera_view &to) {
  to.zoom = from.zoom;
  to.rotation = from.rotation;
  from_raylib(from.target, to.target);
  from_raylib(from.offset, to.offset);
}
