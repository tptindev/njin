#include <stdio.h>

// Function: a return type, a name, parameters. "static" means only this file uses it (see the lesson on multiple files).
static float clampf(float value, float lo, float hi) {
  if (value < lo)
    return lo;
  if (value > hi)
    return hi;
  return value;
}

int main(void) {
  const float gravity = 30.0f;  // any unit: pixels/second^2
  const float dt = 1.0f / 60.0f; // one frame at 60 FPS
  const float ground = 5.0f;

  float y = 0.0f;
  float vy = 0.0f;

  // for: the number of iterations is known up front. Here it only prints the first 5 frames.
  for (int frame = 0; frame < 5; frame++) {
    vy += gravity * dt;
    y += vy * dt;
    printf("frame %d: y=%.4f vy=%.3f\n", frame, y, vy);
  }

  // while: loop until the condition is false. Keep counting until it hits the ground.
  int frames = 5;
  while (y < ground) {
    vy += gravity * dt;
    y += vy * dt;
    frames++;
  }
  y = clampf(y, 0.0f, ground); // do not let it sink below the ground
  printf("hit the ground after %d frames, y=%.2f\n", frames, y);
  return 0;
}
