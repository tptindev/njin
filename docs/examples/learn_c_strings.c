#include <stdio.h>
#include <string.h>

int main(void) {
  // Mảng ký tự: 16 byte của riêng bạn, sửa được. Chuỗi kết thúc bằng '\0'.
  char name[16] = "Sprout";
  // Con trỏ tới chuỗi hằng nằm sẵn trong chương trình: đọc được, KHÔNG được sửa.
  const char *title = "Rung Co Thach";

  printf("strlen(name)=%zu, sizeof(name)=%zu\n", strlen(name), sizeof(name));
  printf("cac byte cua name:");
  for (int i = 0; i < 8; i++)
    printf(" %d", name[i]); // ký tự nào cũng là số; byte cuối chuỗi là 0
  printf("\n");

  // snprintf ghi có giới hạn, luôn thêm '\0', và trả về độ dài CẦN có.
  char line[16];
  int needed = snprintf(line, sizeof(line), "%s la %s", name, title);
  printf("line=\"%s\" (can %d ky tu, buffer %zu)\n", line, needed, sizeof(line));
  if (needed >= (int)sizeof(line))
    printf("bi cat cut!\n");

  // So sánh chuỗi: == so ĐỊA CHỈ, strcmp so NỘI DUNG.
  const char *a = "hi";
  char b[] = "hi";
  printf("a == b: %d, strcmp(a, b) == 0: %d\n", a == (const char *)b, strcmp(a, b) == 0);
  return 0;
}
