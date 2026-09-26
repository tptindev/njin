// Sân chơi shader: nạp một fragment shader từ file, vẽ nó lên cửa sổ, nạp lại khi file đổi.
//
//   playground shader.fs                 chạy bình thường
//   playground shader.fs 3 anh.png 30    chế độ 3, chụp ảnh sau 30 frame rồi thoát (để so sánh)
//   playground shader.fs 2 anh.png 30 1  như trên, số 1 ở cuối: ảnh dùng bộ lọc mượt (bilinear)
//
// Phím: 1, 2, 3 đổi chế độ vẽ. F đổi bộ lọc ảnh (mượt / điểm). R nạp lại shader.
// Uniform mà shader có thể dùng: time (giây), resolution (pixel), mouse (0..1).
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>

// Ảnh thử: một nhân vật nhỏ 48 x 48, nền trong suốt. Tự vẽ nên không cần file.
static Texture2D make_sprite(void) {
  Image img = GenImageColor(48, 48, BLANK);
  ImageDrawRectangle(&img, 14, 36, 8, 8, (Color){110, 70, 40, 255});   // chân
  ImageDrawRectangle(&img, 26, 36, 8, 8, (Color){110, 70, 40, 255});
  ImageDrawCircle(&img, 24, 24, 15, (Color){240, 150, 50, 255});        // thân
  ImageDrawCircle(&img, 18, 20, 4, WHITE);                              // mắt
  ImageDrawCircle(&img, 30, 20, 4, WHITE);
  ImageDrawCircle(&img, 19, 21, 2, BLACK);
  ImageDrawCircle(&img, 31, 21, 2, BLACK);
  ImageDrawRectangle(&img, 12, 4, 24, 6, (Color){200, 50, 60, 255});    // mũ
  ImageDrawRectangle(&img, 16, 2, 16, 6, (Color){200, 50, 60, 255});
  Texture2D tex = LoadTextureFromImage(img);
  UnloadImage(img);
  return tex;
}

// Một cảnh nhỏ để thử hậu kỳ: bầu trời, đất, cây, nhân vật.
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
  bool smooth = argc > 5 && atoi(argv[5]) != 0;   // raylib mặc định nạp ảnh với bộ lọc điểm (nearest)
  SetTextureFilter(sprite, smooth ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);

  Shader shader = LoadShader(NULL, path);
  long stamp = GetFileModTime(path);

  for (int frame = 0; !WindowShouldClose(); frame++) {
    // Nạp lại khi file đổi, hoặc bấm R. Shader lỗi thì giữ bản cũ.
    if (IsKeyPressed(KEY_R) || GetFileModTime(path) != stamp) {
      stamp = GetFileModTime(path);
      Shader next = LoadShader(NULL, path);
      if (IsShaderValid(next)) {   // biên dịch lỗi thì id là 0 và IsShaderValid trả về false
        UnloadShader(shader);
        shader = next;
      } else {
        printf("Shader loi (xem log o tren), giu ban cu.\n");
      }
    }
    if (IsKeyPressed(KEY_ONE)) mode = 1;
    if (IsKeyPressed(KEY_TWO)) mode = 2;
    if (IsKeyPressed(KEY_THREE)) mode = 3;
    if (IsKeyPressed(KEY_F)) {
      smooth = !smooth;
      SetTextureFilter(sprite, smooth ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    }

    // Đưa dữ liệu cho shader. Shader không khai báo uniform nào thì vị trí là -1 và bị bỏ qua.
    float time = (float)GetTime();
    float resolution[2] = {(float)GetScreenWidth(), (float)GetScreenHeight()};
    float mouse[2] = {GetMouseX() / resolution[0], GetMouseY() / resolution[1]};
    if (shot) { mouse[0] = 0.7f; mouse[1] = 0.5f; }   // chế độ chụp ảnh: chuột cố định
    SetShaderValue(shader, GetShaderLocation(shader, "time"), &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, GetShaderLocation(shader, "resolution"), resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader, GetShaderLocation(shader, "mouse"), mouse, SHADER_UNIFORM_VEC2);

    if (mode == 3) {                      // vẽ cảnh vào một ảnh ngoài màn hình trước
      BeginTextureMode(scene);
      draw_scene(sprite, 800, 450);
      EndTextureMode();
    }

    BeginDrawing();
    ClearBackground((Color){30, 30, 40, 255});
    BeginShaderMode(shader);
    if (mode == 1) {                      // ảnh kéo giãn phủ cả cửa sổ: fragTexCoord chạy 0..1
      DrawTexturePro(sprite, (Rectangle){0, 0, 48, 48}, (Rectangle){0, 0, 800, 450}, (Vector2){0, 0}, 0.0f, WHITE);
    } else if (mode == 2) {               // sprite phóng 6 lần ở giữa
      DrawTexturePro(sprite, (Rectangle){0, 0, 48, 48}, (Rectangle){400 - 144, 225 - 144, 288, 288}, (Vector2){0, 0}, 0.0f, WHITE);
    } else {                              // cả cảnh qua shader (chiều cao âm: ảnh ngoài màn hình bị lật dọc)
      DrawTexturePro(scene.texture, (Rectangle){0, 0, 800, -450}, (Rectangle){0, 0, 800, 450}, (Vector2){0, 0}, 0.0f, WHITE);
    }
    EndShaderMode();
    DrawText(TextFormat("che do %d  |  1 2 3: che do  F: loc anh  R: nap lai", mode), 8, 6, 16, WHITE);
    EndDrawing();

    if (shot && frame == shot_frame) {    // chụp ảnh rồi thoát
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
