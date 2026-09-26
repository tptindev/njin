// Lỗi: xin bộ nhớ mà không bao giờ trả (rò rỉ). Mỗi frame mất một ít.
#include <stdio.h>
#include <stdlib.h>

static void spawn_particle(void) {
  float *particle = malloc(4 * sizeof(float)); // x, y, vx, vy
  if (particle == NULL)
    return;
  particle[0] = 1.0f;
  // thiếu free(particle): con trỏ mất khi hàm kết thúc, vùng nhớ thì ở lại
}

int main(void) {
  for (int frame = 0; frame < 3; frame++)
    spawn_particle();
  printf("xong\n");
  return 0;
}
