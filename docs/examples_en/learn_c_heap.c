#include <stdio.h>
#include <stdlib.h>

int main(void) {
  int capacity = 2;
  int count = 0;
  int *values = malloc((size_t)capacity * sizeof(int)); // ask for memory on the heap
  if (values == NULL) {                                  // always check: memory can run out
    printf("out of memory\n");
    return 1;
  }

  for (int i = 1; i <= 5; i++) {
    if (count == capacity) { // full: ask for a block twice as big (realloc keeps the old contents)
      capacity *= 2;
      int *bigger = realloc(values, (size_t)capacity * sizeof(int));
      if (bigger == NULL) {
        free(values);
        return 1;
      }
      values = bigger;
      printf("grown to %d elements\n", capacity);
    }
    values[count++] = i * 10;
  }

  for (int i = 0; i < count; i++)
    printf("%d ", values[i]);
  printf("\n");

  free(values);   // give it back exactly once
  values = NULL;  // so it is not used again by accident
  return 0;
}
