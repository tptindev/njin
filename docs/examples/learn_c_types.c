#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
  int lives = 3;              // số nguyên có dấu, thường 32 bit
  float speed = 110.5f;       // số thực 32 bit ('f' cuối: hằng float)
  char grade = 'A';           // một ký tự, thực chất là một số nhỏ
  bool alive = true;          // cần <stdbool.h>

  int32_t score = 1250;       // đúng 32 bit có dấu (cần <stdint.h>)
  uint8_t red = 255;          // đúng 8 bit không dấu: 0..255

  printf("lives=%d speed=%f speed(2 so)=%.2f\n", lives, speed, speed);
  printf("grade=%c ma so=%d alive=%d\n", grade, grade, alive);
  printf("score=%d red=%u hex=%x\n", score, red, red);

  // Phép chia số nguyên bỏ phần thập phân.
  printf("7 / 2 = %d, 7 / 2.0 = %.1f\n", 7 / 2, 7 / 2.0);

  // Số không dấu quay vòng khi vượt giới hạn (điều này được C bảo đảm).
  red = (uint8_t)(red + 1);
  printf("255 + 1 trong uint8_t = %u\n", red);

  // Kích thước, tính bằng byte. %zu là chỉ định cho kiểu size_t của sizeof.
  printf("sizeof: char=%zu int=%zu float=%zu double=%zu int64_t=%zu\n", sizeof(char), sizeof(int),
         sizeof(float), sizeof(double), sizeof(int64_t));
  return 0;
}
