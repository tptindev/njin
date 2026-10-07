#include <stdio.h>
#include <string.h>

int main(void) {
  // Character array: 16 bytes of your own, you can change them. The string ends with '\0'.
  char name[16] = "Sprout";
  // Pointer to a constant string already in the program: you can read it, you may NOT change it.
  const char *title = "Rung Co Thach";

  printf("strlen(name)=%zu, sizeof(name)=%zu\n", strlen(name), sizeof(name));
  printf("bytes of name:");
  for (int i = 0; i < 8; i++)
    printf(" %d", name[i]); // every character is a number; the byte at the end of the string is 0
  printf("\n");

  // snprintf writes with a limit, always adds '\0', and returns the length it NEEDED.
  char line[16];
  int needed = snprintf(line, sizeof(line), "%s is %s", name, title);
  printf("line=\"%s\" (needs %d chars, buffer %zu)\n", line, needed, sizeof(line));
  if (needed >= (int)sizeof(line))
    printf("truncated!\n");

  // Comparing strings: == compares ADDRESSES, strcmp compares CONTENTS.
  const char *a = "hi";
  char b[] = "hi";
  printf("a == b: %d, strcmp(a, b) == 0: %d\n", a == (const char *)b, strcmp(a, b) == 0);
  return 0;
}
