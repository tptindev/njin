// Shader playground: load a fragment shader from a file, draw it on the window, reload it when the file changes.
//
//   playground shader.fs                 run normally
//   playground shader.fs 3 shot.png 30   mode 3, take a screenshot after 30 frames, then quit (for comparing)
//   playground shader.fs 2 shot.png 30 1 as above, the 1 at the end: the image uses the smooth (bilinear) filter
//
// Keys: 1, 2, 3 switch the draw mode. F toggles the image filter (smooth / point). R reloads the shader.
// Uniforms a shader can use: time (seconds), resolution (pixels), mouse (0..1).
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>

// Test image: a small 48 x 48 character, transparent background. Drawn in code, so no file needed.
static Texture2D make_sprite(void) {
  Image img = GenImageColor(48, 48, BLANK);
  ImageDrawRectangle(&img, 14, 36, 8, 8, (Color){110, 70, 40, 255});   // feet
  ImageDrawRectangle(&img, 26, 36, 8, 8, (Color){110, 70, 40, 255});
  ImageDrawCircle(&img, 24, 24, 15, (Color){240, 150, 50, 255});        // body
  ImageDrawCircle(&img, 18, 20, 4, WHITE);                              // eyes
  ImageDrawCircle(&img, 30, 20, 4, WHITE);
  ImageDrawCircle(&img, 19, 21, 2, BLACK);
  ImageDrawCircle(&img, 31, 21, 2, BLACK);
  ImageDrawRectangle(&img, 12, 4, 24, 6, (Color){200, 50, 60, 255});    // hat
  ImageDrawRectangle(&img, 16, 2, 16, 6, (Color){200, 50, 60, 255});
  Texture2D tex = LoadTextureFromImage(img);
  UnloadImage(img);
  return tex;
}

// A small scene to try post-processing on: sky, ground, tree, character.
static void draw_scene(Texture2D sprite, int w, int h) {
  DrawRectangleGradientV(0, 0, w, h * 2 / 3, (Color){90, 170, 230, 255}, (Color){200, 230, 250, 255});
  DrawRectangle(0, h * 2 / 3, w, h / 3, (Color){70, 140, 70, 255});
  DrawCircle(w - 120, 90, 40, (Color){255, 230, 120, 255});
  DrawRectangle(140, h * 2 / 3 - 70, 16, 70, (Color){110, 70, 40, 255});
  DrawCircle(148, h * 2 / 3 - 90, 46, (Color){40, 110, 50, 255});
  DrawTextureEx(sprite, (Vector2){w / 2.0f - 96, h * 2 / 3.0f - 150}, 0.0f, 4.0f, WHITE);
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "shader.fs";
  int mode = argc > 2 ? atoi(argv[2]) : 1;
  const char *shot = argc > 3 ? argv[3] : NULL;
  const int shot_frame = argc > 4 ? atoi(argv[4]) : 20;

  InitWindow(800, 450, "Shader playground");
  SetTargetFPS(60);

  Texture2D sprite = make_sprite();
  RenderTexture2D scene = LoadRenderTexture(800, 450);
  bool smooth = argc > 5 && atoi(argv[5]) != 0;   // raylib loads images with the point (nearest) filter by default
  SetTextureFilter(sprite, smooth ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);

  Shader shader = LoadShader(NULL, path);
  long stamp = GetFileModTime(path);

  for (int frame = 0; !WindowShouldClose(); frame++) {
    // Reload when the file changes, or when R is pressed. A broken shader keeps the old one.
    if (IsKeyPressed(KEY_R) || GetFileModTime(path) != stamp) {
      stamp = GetFileModTime(path);
      Shader next = LoadShader(NULL, path);
      if (IsShaderValid(next)) {   // on a compile error the id is 0 and IsShaderValid returns false
        UnloadShader(shader);
        shader = next;
      } else {
        printf("Shader error (see the log above), keeping the old one.\n");
      }
    }
    if (IsKeyPressed(KEY_ONE)) mode = 1;
    if (IsKeyPressed(KEY_TWO)) mode = 2;
    if (IsKeyPressed(KEY_THREE)) mode = 3;
    if (IsKeyPressed(KEY_F)) {
      smooth = !smooth;
      SetTextureFilter(sprite, smooth ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    }

    // Pass data to the shader. If the shader does not declare a uniform, its location is -1 and it is skipped.
    float time = (float)GetTime();
    float resolution[2] = {(float)GetScreenWidth(), (float)GetScreenHeight()};
    float mouse[2] = {GetMouseX() / resolution[0], GetMouseY() / resolution[1]};
    if (shot) { mouse[0] = 0.7f; mouse[1] = 0.5f; }   // screenshot mode: the mouse is fixed
    SetShaderValue(shader, GetShaderLocation(shader, "time"), &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "resolution"), resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, GetShaderLocation(shader, "mouse"), mouse, SHADER_UNIFORM_VEC2);

    if (mode == 3) {                      // first draw the scene into an off-screen image
      BeginTextureMode(scene);
      draw_scene(sprite, 800, 450);
      EndTextureMode();
    }

    BeginDrawing();
    ClearBackground((Color){30, 30, 40, 255});
    BeginShaderMode(shader);
    if (mode == 1) {                      // image stretched over the whole window: fragTexCoord runs 0..1
      DrawTexturePro(sprite, (Rectangle){0, 0, 48, 48}, (Rectangle){0, 0, 800, 450}, (Vector2){0, 0}, 0.0f, WHITE);
    } else if (mode == 2) {               // sprite scaled 6 times in the middle
      DrawTexturePro(sprite, (Rectangle){0, 0, 48, 48}, (Rectangle){400 - 144, 225 - 144, 288, 288}, (Vector2){0, 0}, 0.0f, WHITE);
    } else {                              // the whole scene through the shader (negative height: the off-screen image is flipped vertically)
      DrawTexturePro(scene.texture, (Rectangle){0, 0, 800, -450}, (Rectangle){0, 0, 800, 450}, (Vector2){0, 0}, 0.0f, WHITE);
    }
    EndShaderMode();
    DrawText(TextFormat("mode %d  |  1 2 3: mode  F: filter  R: reload", mode), 8, 6, 16, WHITE);
    EndDrawing();

    if (shot && frame == shot_frame) {    // take a screenshot, then quit
      Image img = LoadImageFromScreen();
      ExportImage(img, shot);
      UnloadImage(img);
      break;
    }
  }
  UnloadShader(shader);
  UnloadTexture(sprite);
  UnloadRenderTexture(scene);
  CloseWindow();
  return 0;
}
