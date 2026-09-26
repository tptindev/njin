// Cùng một cú nhảy, mô phỏng ở nhiều FPS, hai cách chạy vật lý.
#include <cstdio>
#include <initializer_list>

constexpr float gravity = 1000.0f;   // pixel / giây^2
constexpr float jump_speed = 300.0f; // pixel / giây

struct body {
  float y = 0.0f; // độ cao so với mặt đất
  float v = jump_speed;
};

// Một bước vật lý: cập nhật vận tốc rồi vị trí.
void step(body &b, float dt) {
  b.v -= gravity * dt;
  b.y += b.v * dt;
}

// Cách 1: bước vật lý = thời gian của frame. Kết quả phụ thuộc FPS.
float peak_variable(float fps) {
  const float frame = 1.0f / fps;
  body b;
  float peak = 0.0f;
  for (float t = 0.0f; t < 1.0f; t += frame) {
    step(b, frame);
    if (b.y > peak)
      peak = b.y;
  }
  return peak;
}

// Cách 2: bước vật lý cố định, gom thời gian rồi chạy đủ số bước.
// Đây là cách njin làm với phase_fixed_update.
float peak_fixed(float fps) {
  const float frame = 1.0f / fps;
  const float fixed_dt = 1.0f / 60.0f;
  const int max_steps = 8; // sau một frame quá dài, bỏ phần dồn lại
  body b;
  float peak = 0.0f;
  float accumulator = 0.0f;
  for (float t = 0.0f; t < 1.0f; t += frame) {
    accumulator += frame;
    int steps = 0;
    while (accumulator >= fixed_dt && steps < max_steps) {
      step(b, fixed_dt);
      accumulator -= fixed_dt;
      steps++;
      if (b.y > peak)
        peak = b.y;
    }
    if (steps == max_steps && accumulator >= fixed_dt)
      accumulator = 0.0f;
  }
  return peak;
}

int main() {
  std::printf("%6s  %-16s  %-16s\n", "FPS", "bước = frame", "bước cố định");
  for (const float fps : {20.0f, 30.0f, 60.0f, 144.0f})
    std::printf("%6.0f  %-16.2f  %-16.2f\n", fps, peak_variable(fps), peak_fixed(fps));
}
