#include <cstdio>
#include <njin.h>

using namespace njin;

constexpr i32 water = 0, sand = 1, grass = 2;

void print(const char *title, const tile_grid &g) {
  std::printf("%s\n", title);
  for (i32 y = 0; y < g.height; y++) {
    for (i32 x = 0; x < g.width; x++)
      std::putchar(".,#"[g.get(x, y)]);
    std::putchar('\n');
  }
}

int main() {
  // Nhiễu vụn cho ra một bản đồ lởm chởm: nước, cát, cỏ chấm lẻ khắp nơi.
  const noise_desc noise{.seed = 11, .frequency = 0.16f, .octaves = 3};
  tile_grid g = tile_grid_make(60, 14);
  for (i32 y = 0; y < g.height; y++)
    for (i32 x = 0; x < g.width; x++) {
      const f32 v = noise_2d(noise, (f32)x, (f32)y * 2.0f);
      g.set(x, y, v < 0.47f ? water : (v < 0.52f ? sand : grass));
    }
  print("Trước khi áp luật", g);

  // 1. Làm mượt: mỗi ô theo loại chiếm đa số quanh nó. Chấm lẻ và răng cưa biến mất.
  grid_majority(g, 2);
  // 2. Bỏ vùng nhỏ: mảng dưới 12 ô hoà vào loại ô bao quanh.
  grid_merge_small(g, 12);
  // 3. Viền: cỏ chạm nước thì thành cát, dày một ô.
  grid_border(g, grass, water, sand);
  print("Sau khi áp luật", g);
}
