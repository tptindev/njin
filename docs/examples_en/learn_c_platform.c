#include <stdio.h>

// Choose code by platform: the compiler predefines a few names (_WIN32, __linux__, __APPLE__).
#if defined(_WIN32)
#define PLATFORM "Windows"
#elif defined(__linux__)
#define PLATFORM "Linux"
#elif defined(__APPLE__)
#define PLATFORM "macOS"
#else
#define PLATFORM "other"
#endif

// Turn a feature on or off from the command line: gcc -DDEBUG_LOG ...
#ifdef DEBUG_LOG
#define LOG(msg) printf("[log] %s\n", msg)
#else
#define LOG(msg) ((void)0)
#endif

#define MAX_SPRITES 8
#define SQUARE_BAD(x) x * x
#define SQUARE_OK(x) ((x) * (x))

int main(void) {
  printf("platform: %s\n", PLATFORM);
  LOG("running");
  printf("MAX_SPRITES=%d\n", MAX_SPRITES);
  printf("SQUARE_BAD(1 + 2) = %d, SQUARE_OK(1 + 2) = %d\n", SQUARE_BAD(1 + 2), SQUARE_OK(1 + 2));
  return 0;
}
