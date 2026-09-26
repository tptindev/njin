// Lỗi: dùng bộ nhớ sau khi đã trả lại (use-after-free).
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  int *lives = malloc(sizeof(int));
  if (lives == NULL)
    return 1;
  *lives = 3;
  free(lives);
  printf("lives=%d\n", *lives); // sai: vùng này không còn thuộc về bạn
  return 0;
}
