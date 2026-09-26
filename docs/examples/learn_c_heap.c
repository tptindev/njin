#include <stdio.h>
#include <stdlib.h>

int main(void) {
  int capacity = 2;
  int count = 0;
  int *values = malloc((size_t)capacity * sizeof(int)); // xin bộ nhớ trên heap
  if (values == NULL) {                                  // luôn kiểm tra: có thể hết bộ nhớ
    printf("het bo nho\n");
    return 1;
  }

  for (int i = 1; i <= 5; i++) {
    if (count == capacity) { // đầy: xin vùng lớn gấp đôi (realloc giữ nội dung cũ)
      capacity *= 2;
      int *bigger = realloc(values, (size_t)capacity * sizeof(int));
      if (bigger == NULL) {
        free(values);
        return 1;
      }
      values = bigger;
      printf("mo rong len %d phan tu\n", capacity);
    }
    values[count++] = i * 10;
  }

  for (int i = 0; i < count; i++)
    printf("%d ", values[i]);
  printf("\n");

  free(values);   // trả lại đúng một lần
  values = NULL;  // để không vô tình dùng lại
  return 0;
}
