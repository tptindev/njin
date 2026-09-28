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
  // Fine-grained noise gives a ragged map: water, sand and grass dotted everywhere.
  const noise_desc noise{.seed = 11, .frequency = 0.16f, .octaves = 3};
  tile_grid g = tile_grid_make(60, 14);
  for (i32 y = 0; y < g.height; y++)
    for (i32 x = 0; x < g.width; x++) {
      const f32 v = noise_2d(noise, (f32)x, (f32)y * 2.0f);
      g.set(x, y, v < 0.47f ? water : (v < 0.52f ? sand : grass));
    }
  print("Before applying the rules", g);

  // 1. Smoothing: each tile takes the majority type around it. Stray dots and jagged edges disappear.
  grid_majority(g, 2);
  // 2. Drop small regions: patches under 12 tiles blend into the surrounding type.
  grid_merge_small(g, 12);
  // 3. Border: grass touching water becomes sand, one tile thick.
  grid_border(g, grass, water, sand);
  print("After applying the rules", g);
}
