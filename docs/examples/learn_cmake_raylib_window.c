#include <raylib.h>

int main(void) {
  InitWindow(640, 360, "Cua so raylib");
  SetTargetFPS(60);

  while (!WindowShouldClose()) { // Esc hoac nut dong cua so
    BeginDrawing();
    ClearBackground((Color){24, 26, 38, 255});
    DrawText("Cua so dau tien", 24, 24, 32, RAYWHITE);
    DrawFPS(24, 320);
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
