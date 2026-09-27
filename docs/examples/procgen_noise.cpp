#include <cstdio>
#include <njin.h>

using namespace njin;

// In một trường nhiễu thành chữ: '#' là nơi nhiễu cao hơn `level`, '.' là nơi thấp hơn.
void show(const char *title, const noise_desc &d, f32 level) {
  std::printf("%s\n", title);
  for (i32 y = 0; y < 14; y++) {
    for (i32 x = 0; x < 60; x++)
      std::putchar(noise_2d(d, (f32)x, (f32)y * 2.0f) > level ? '#' : '.'); // *2: ký tự cao gấp đôi rộng
    std::putchar('\n');
  }
}

int main() {
  // Đơn giản nhất: một lớp, chỉ có seed và tần số. Vùng to, trơn.
  show("1. một lớp, tần số 0.06", {.seed = 4, .frequency = 0.06f, .fractal = fractal_none}, 0.5f);

  // Thêm lớp (octave): cùng vùng to đó, nhưng mép có chi tiết nhỏ.
  show("2. fBm, 5 lớp", {.seed = 4, .frequency = 0.06f, .octaves = 5}, 0.5f);

  // gain lớn làm các lớp nhỏ mạnh lên: mép gồ ghề hơn.
  show("3. fBm, 5 lớp, gain 0.7", {.seed = 4, .frequency = 0.06f, .octaves = 5, .gain = 0.7f}, 0.5f);

  // Uốn toạ độ (warp): viền cong queo như bản đồ thật.
  show("4. fBm, 5 lớp, warp 8", {.seed = 4, .frequency = 0.06f, .octaves = 5, .warp = 8.0f}, 0.5f);

  // Gờ nhọn: lấy vùng cao thì ra sống núi hẹp, lấy vùng thấp ra sông.
  show("5. ridged, 4 lớp", {.seed = 4, .frequency = 0.05f, .fractal = fractal_ridged, .octaves = 4}, 0.72f);
}
