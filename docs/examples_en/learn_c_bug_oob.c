// Bug: writing past the end of an array. A 4-element array has indices 0..3, but the loop runs to 4.
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  int *scores = malloc(4 * sizeof(int));
  if (scores == NULL)
    return 1;
  for (int i = 0; i <= 4; i++) // wrong: it must be i < 4
    scores[i] = i * 10;
  printf("done\n");
  free(scores);
  return 0;
}
