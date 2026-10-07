// Bug: using a variable that was never given a value. It holds garbage left over in memory.
#include <stdio.h>

int main(int argc, char **argv) {
  (void)argv;
  int bonus;
  if (argc > 5)
    bonus = 100;
  printf("bonus=%d\n", bonus); // if argc <= 5, bonus was never assigned
  return 0;
}
