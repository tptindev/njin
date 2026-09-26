#include <stdio.h>

// Hàm: có kiểu trả về, tên, tham số. "static" nghĩa là chỉ file này dùng (xem bài về nhiều file).
static float clampf(float value, float lo, float hi) {
  if (value < lo)
    return lo;
  if (value > hi)
    return hi;
  return value;
}

int main(void) {
  const float gravity = 30.0f;  // đơn vị tùy ý: pixel/giây^2
  const float dt = 1.0f / 60.0f; // một frame ở 60 FPS
  const float ground = 5.0f;

  float y = 0.0f;
  float vy = 0.0f;

  // for: biết trước số lần lặp. Ở đây chỉ in 5 frame đầu.
  for (int frame = 0; frame < 5; frame++) {
    vy += gravity * dt;
    y += vy * dt;
    printf("frame %d: y=%.4f vy=%.3f\n", frame, y, vy);
  }

  // while: lặp đến khi điều kiện sai. Đếm tiếp đến khi chạm đất.
  int frames = 5;
  while (y < ground) {
    vy += gravity * dt;
    y += vy * dt;
    frames++;
  }
  y = clampf(y, 0.0f, ground); // không để lọt xuống dưới đất
  printf("cham dat sau %d frame, y=%.2f\n", frames, y);
  return 0;
}
