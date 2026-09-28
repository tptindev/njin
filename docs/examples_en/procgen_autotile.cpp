#include <cstdio>
#include <njin.h>

using namespace njin;

// In the grid, ground is only a number "on paper". The ground's 47-tile rounded-corner set, say, starts at tile 100 of the tileset.
constexpr i32 ground = 1;

int main() {
  // An L-shaped island, written as text to be easy to read. '#' is ground.
  tile_grid g = tile_grid_from_text(
      "..........\n"
      "..####....\n"
      ".######...\n"
      ".######...\n"
      "..#####...\n"
      "....####..\n"
      "....####..\n"
      "..........\n",
      {{'#', ground}});

  // Which tile joins which: each ground tile becomes its index in the 47-tile set, chosen from its eight neighbors.
  const tile_grid before = g;
  const i32 n = grid_autotile(g, {{.tiles = {ground}, .base = 100}});
  std::printf("%d ground tiles replaced by tiles of the set\n\n", n);

  for (i32 y = 0; y < g.height; y++) {
    for (i32 x = 0; x < g.width; x++) {
      if (before.get(x, y) == -1)
        std::printf("  . ");
      else
        std::printf("%3d ", g.get(x, y) - 100);
    }
    std::putchar('\n');
  }

  // The same mask, computed by hand: tile (2, 1) is the top-left corner, with only the right, bottom and bottom-right neighbors.
  const u8 mask = neighbor_right | neighbor_down | neighbor_down_right;
  std::printf("\ntile (2, 1): mask %d -> set tile %d (match: %s)\n", mask, autotile_index(mask),
              g.get(2, 1) == 100 + autotile_index(mask) ? "yes" : "no");
}
