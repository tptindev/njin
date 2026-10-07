#include "learn_c_vec.h"
#include <math.h>

// Definition: the only place where this variable gets real memory.
int vec2_calls = 0;

// static: only this file sees the function square. Other files cannot call it, and the name does not clash.
static float square(float v) { return v * v; }

vec2 vec2_add(vec2 a, vec2 b) {
  vec2_calls++;
  return (vec2){a.x + b.x, a.y + b.y};
}

float vec2_length(vec2 v) {
  vec2_calls++;
  return sqrtf(square(v.x) + square(v.y));
}
