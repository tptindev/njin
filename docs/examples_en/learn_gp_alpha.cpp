// Answer to exercise 2: interpolate the draw position with alpha, at 144 FPS.
#include <cstdio>
constexpr float gravity = 1000.0f, jump_speed = 300.0f;
struct body { float y = 0.0f, v = jump_speed, prev_y = 0.0f; };
void step(body &b, float dt) { b.prev_y = b.y; b.v -= gravity * dt; b.y += b.v * dt; }

int main() {
  const float fps = 144.0f, frame = 1.0f / fps, fixed_dt = 1.0f / 60.0f;
  body b;
  float accumulator = 0.0f;
  int frames = 0, y_changed = 0, draw_changed = 0;
  float last_y = 0.0f, last_draw = 0.0f;
  bool between = true;
  for (float t = 0.0f; t < 0.5f; t += frame) {
    accumulator += frame;
    while (accumulator >= fixed_dt) { step(b, fixed_dt); accumulator -= fixed_dt; }
    const float alpha = accumulator / fixed_dt;                    // leftover fraction of a tick, 0..1
    const float draw_y = b.prev_y + (b.y - b.prev_y) * alpha;      // the position to DRAW
    const float lo = b.prev_y < b.y ? b.prev_y : b.y, hi = b.prev_y < b.y ? b.y : b.prev_y;
    if (draw_y < lo - 1e-4f || draw_y > hi + 1e-4f) between = false;
    if (b.y != last_y) y_changed++;
    if (draw_y != last_draw) draw_changed++;
    last_y = b.y; last_draw = draw_y; frames++;
  }
  std::printf("frames: %d, y changed in %d frames, draw_y changed in %d frames, draw_y always between prev and y: %s\n",
              frames, y_changed, draw_changed, between ? "yes" : "NO");
}
