// Compile the same high-level shadow regression as the desktop executable.
#define main njin_android_shadow_test
#include "../../src/engine/tests/shadows.cpp"
#undef main
