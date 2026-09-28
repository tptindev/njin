#include <cstdio>
#include <njin.h>

using namespace njin;

constexpr i32 sky = -1, grass = 0, dirt = 1, stone = 2;

void print(const tile_grid &g) {
  for (i32 y = 0; y < g.height; y++) {
    for (i32 x = 0; x < g.width; x++) {
      const i32 t = g.get(x, y);
      std::putchar(t < 0 ? ' ' : "gds"[t]);
    }
    std::putchar('\n');
  }
}

int main() {
  // 1. Sample: a piece of terrain drawn by a person. WFC learns from it which tile may stand next to which in each direction (sky can only
  //    be above grass or above sky, grass sits on dirt, dirt sits on stone...), and which tiles are more common than others.
  const tile_grid sample = tile_grid_from_text(
      "                              \n"
      "                    ggg       \n"
      "gggggggg    ggggggggddggggggg \n"
      "ddddddddgggggddddddddddddddddg\n"
      "dddddddddddddddddddddddddddddd\n"
      "ssdddddsssdddddddddsssssdddsss\n"
      "ssssssssssssssssssssssssssssss\n",
      {{'g', grass}, {'d', dirt}, {'s', stone}});
  const wfc_rules rules = wfc_learn(sample);

  // 2. Generate. The same seed gives the same result; change the seed for a different version.
  wfc_desc d{.width = 60, .height = 20, .seed = 8};

  // 3. Constraint: the top row is always sky, the bottom row is always stone.
  d.allowed = [&](i32, i32 y, i32 tile) {
    if (y == 0)
      return tile == sky;
    if (y == d.height - 1)
      return tile == stone;
    return true;
  };

  tile_grid out;
  if (!wfc_generate(rules, d, out)) {
    std::printf("rules too strict or constraints contradict each other\n");
    return 1;
  }
  print(out);
}
