// Lỗi: ghi ra ngoài mảng. Mảng 4 phần tử có chỉ số 0..3, nhưng vòng lặp chạy tới 4.
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  int *scores = malloc(4 * sizeof(int));
  if (scores == NULL)
    return 1;
  for (int i = 0; i <= 4; i++) // sai: phải là i < 4
    scores[i] = i * 10;
  printf("xong\n");
  free(scores);
  return 0;
}
