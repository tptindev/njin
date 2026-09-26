#include <stdio.h>

// Nhận địa chỉ của một float, nên sửa được chính biến của người gọi.
static void scale(float *value, float factor) { *value = *value * factor; }

// "const int *": chỉ đọc được các phần tử. Mảng truyền vào hàm chỉ còn là con trỏ.
static int sum(const int *values, int count) {
  int total = 0;
  for (int i = 0; i < count; i++)
    total += values[i];
  return total;
}

int main(void) {
  int lives = 3;
  int *p = &lives; // p giữ địa chỉ của lives
  *p = 5;          // đi tới địa chỉ đó và ghi: lives đổi theo
  printf("lives=%d, *p=%d, p==&lives: %d\n", lives, *p, p == &lives);

  float speed = 100.0f;
  scale(&speed, 1.5f);
  printf("speed=%.1f\n", speed);

  // Mảng: các phần tử nằm sát nhau trong bộ nhớ.
  int scores[5] = {10, 20, 30, 40, 50};
  printf("sizeof(scores)=%zu, so phan tu=%zu\n", sizeof(scores), sizeof(scores) / sizeof(scores[0]));
  printf("khoang cach giua scores[1] va scores[0] = %td byte\n", (char *)&scores[1] - (char *)&scores[0]);

  // scores[i] chính là *(scores + i): cộng con trỏ tính theo phần tử, không theo byte.
  printf("scores[2]=%d, *(scores+2)=%d\n", scores[2], *(scores + 2));

  // Khi truyền vào hàm, mảng "suy biến" thành con trỏ tới phần tử đầu.
  printf("sum=%d\n", sum(scores, 5));

  // Con trỏ không trỏ tới đâu cả: NULL. Kiểm tra trước khi dùng.
  int *nothing = NULL;
  if (nothing == NULL)
    printf("nothing la NULL, khong duoc dung *nothing\n");
  return 0;
}
