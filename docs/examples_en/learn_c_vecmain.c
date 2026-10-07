#include "learn_c_vec.h"
#include <stdio.h>

int main(void) {
  vec2 a = {3.0f, 0.0f};
  vec2 b = {0.0f, 4.0f};
  vec2 sum = vec2_add(a, b);
  printf("sum=(%.0f, %.0f) length=%.1f\n", sum.x, sum.y, vec2_length(sum));
  printf("function calls: %d\n", vec2_calls);
  return 0;
}
