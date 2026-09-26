// Lỗi: giữ lại địa chỉ của biến cục bộ. Biến chết khi hàm kết thúc, con trỏ thì ở lại.
#include <stdio.h>

static const char *saved;

static void remember_label(int level) {
  char label[32];
  snprintf(label, sizeof(label), "Level %d", level);
  saved = label; // sai: label nằm trên stack của remember_label
}

int main(void) {
  remember_label(3);
  printf("%s\n", saved); // đọc một vùng stack đã được trả lại
  return 0;
}
