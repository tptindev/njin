// Bug: using memory after it has been given back (use-after-free).
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  int *lives = malloc(sizeof(int));
  if (lives == NULL)
    return 1;
  *lives = 3;
  free(lives);
  printf("lives=%d\n", *lives); // wrong: this memory no longer belongs to you
  return 0;
}
