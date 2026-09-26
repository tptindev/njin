#include "learn_c_vec.h"
#include <math.h>

// Định nghĩa (definition): chỗ duy nhất biến này có bộ nhớ thật.
int vec2_calls = 0;

// static: chỉ file này thấy hàm square. File khác không gọi được, và không đụng tên.
static float square(float v) { return v * v; }

vec2 vec2_add(vec2 a, vec2 b) {
  vec2_calls++;
  return (vec2){a.x + b.x, a.y + b.y};
}

float vec2_length(vec2 v) {
  vec2_calls++;
  return sqrtf(square(v.x) + square(v.y));
}
