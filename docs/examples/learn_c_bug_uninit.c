// Lỗi: dùng biến chưa gán giá trị. Nó chứa rác còn lại trong bộ nhớ.
#include <stdio.h>

int main(int argc, char **argv) {
  (void)argv;
  int bonus;
  if (argc > 5)
    bonus = 100;
  printf("bonus=%d\n", bonus); // nếu argc <= 5 thì bonus chưa bao giờ được gán
  return 0;
}
