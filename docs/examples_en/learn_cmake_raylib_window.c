#include <raylib.h>

int main(void) {
  InitWindow(640, 360, "raylib window");
  SetTargetFPS(60);

  while (!WindowShouldClose()) { // Esc or the window's close button
    BeginDrawing();
    ClearBackground((Color){24, 26, 38, 255});
    DrawText("First window", 24, 24, 32, RAYWHITE);
    DrawFPS(24, 320);
    EndDrawing();
  }

  CloseWindow();
  return 0;
}
