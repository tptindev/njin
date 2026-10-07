#include <stdio.h>

// Takes the address of a float, so it can change the caller's own variable.
static void scale(float *value, float factor) { *value = *value * factor; }

// "const int *": the elements can only be read. An array passed to a function is only a pointer.
static int sum(const int *values, int count) {
  int total = 0;
  for (int i = 0; i < count; i++)
    total += values[i];
  return total;
}

int main(void) {
  int lives = 3;
  int *p = &lives; // p holds the address of lives
  *p = 5;          // go to that address and write: lives changes with it
  printf("lives=%d, *p=%d, p==&lives: %d\n", lives, *p, p == &lives);

  float speed = 100.0f;
  scale(&speed, 1.5f);
  printf("speed=%.1f\n", speed);

  // Array: the elements sit right next to each other in memory.
  int scores[5] = {10, 20, 30, 40, 50};
  printf("sizeof(scores)=%zu, elements=%zu\n", sizeof(scores), sizeof(scores) / sizeof(scores[0]));
  printf("distance between scores[1] and scores[0] = %td bytes\n", (char *)&scores[1] - (char *)&scores[0]);

  // scores[i] is exactly *(scores + i): pointer arithmetic counts in elements, not bytes.
  printf("scores[2]=%d, *(scores+2)=%d\n", scores[2], *(scores + 2));

  // Passed to a function, an array "decays" into a pointer to its first element.
  printf("sum=%d\n", sum(scores, 5));

  // A pointer that points nowhere: NULL. Check before using it.
  int *nothing = NULL;
  if (nothing == NULL)
    printf("nothing is NULL, do not use *nothing\n");
  return 0;
}
