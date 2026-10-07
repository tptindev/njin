#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
  int lives = 3;              // signed integer, usually 32 bits
  float speed = 110.5f;       // 32-bit real number (trailing 'f': a float constant)
  char grade = 'A';           // one character, really a small number
  bool alive = true;          // needs <stdbool.h>

  int32_t score = 1250;       // exactly 32 bits, signed (needs <stdint.h>)
  uint8_t red = 255;          // exactly 8 bits, unsigned: 0..255

  printf("lives=%d speed=%f speed(2 digits)=%.2f\n", lives, speed, speed);
  printf("grade=%c code=%d alive=%d\n", grade, grade, alive);
  printf("score=%d red=%u hex=%x\n", score, red, red);

  // Integer division drops the fractional part.
  printf("7 / 2 = %d, 7 / 2.0 = %.1f\n", 7 / 2, 7 / 2.0);

  // Unsigned numbers wrap around when they pass their limit (C guarantees this).
  red = (uint8_t)(red + 1);
  printf("255 + 1 in uint8_t = %u\n", red);

  // Sizes, in bytes. %zu is the specifier for sizeof's type, size_t.
  printf("sizeof: char=%zu int=%zu float=%zu double=%zu int64_t=%zu\n", sizeof(char), sizeof(int),
         sizeof(float), sizeof(double), sizeof(int64_t));
  return 0;
}
