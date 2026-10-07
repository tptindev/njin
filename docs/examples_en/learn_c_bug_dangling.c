// Bug: keeping the address of a local variable. The variable dies when the function ends, the pointer stays.
#include <stdio.h>

static const char *saved;

static void remember_label(int level) {
  char label[32];
  snprintf(label, sizeof(label), "Level %d", level);
  saved = label; // wrong: label lives on remember_label's stack
}

int main(void) {
  remember_label(3);
  printf("%s\n", saved); // reads stack memory that has already been given back
  return 0;
}
