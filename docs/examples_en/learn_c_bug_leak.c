// Bug: asking for memory and never giving it back (a leak). A little is lost every frame.
#include <stdio.h>
#include <stdlib.h>

static void spawn_particle(void) {
  float *particle = malloc(4 * sizeof(float)); // x, y, vx, vy
  if (particle == NULL)
    return;
  particle[0] = 1.0f;
  // missing free(particle): the pointer is lost when the function ends, the memory stays
}

int main(void) {
  for (int frame = 0; frame < 3; frame++)
    spawn_particle();
  printf("done\n");
  return 0;
}
