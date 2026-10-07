// The same jump, simulated at several FPS, with two ways of running physics.
#include <cstdio>
#include <initializer_list>

constexpr float gravity = 1000.0f;   // pixels / second^2
constexpr float jump_speed = 300.0f; // pixels / second

struct body {
  float y = 0.0f; // height above the ground
  float v = jump_speed;
};

// One physics step: update the velocity, then the position.
void step(body &b, float dt) {
  b.v -= gravity * dt;
  b.y += b.v * dt;
}

// Way 1: physics step = frame time. The result depends on FPS.
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

// Way 2: fixed physics step, accumulate time and then run enough steps.
// This is what njin does with phase_fixed_update.
float peak_fixed(float fps) {
  const float frame = 1.0f / fps;
  const float fixed_dt = 1.0f / 60.0f;
  const int max_steps = 8; // after a frame that is too long, drop the backlog
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
  std::printf("%6s  %-16s  %-16s\n", "FPS", "step = frame", "fixed step");
  for (const float fps : {20.0f, 30.0f, 60.0f, 144.0f})
    std::printf("%6.0f  %-16.2f  %-16.2f\n", fps, peak_variable(fps), peak_fixed(fps));
}
