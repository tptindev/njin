#include <bit>
#include <cstdio>
#include <njin.h>

using namespace njin;

// 16 road tiles, numbered exactly like the autotile_edges set: bit 1 is a road going up, 2 to the right, 4 down, 8 to the left.
// Tile 0 is grass (no road), tile 5 is a vertical road, tile 10 is a horizontal road, tile 15 is a crossroads.
constexpr i32 up = 1, right = 2, down = 4, left = 8;

int main() {
  wfc_rules rules;
  // Frequency: lots of grass and straight roads, few T-junctions, almost no crossroads or dead ends.
  for (i32 t = 0; t < 16; t++) {
    const i32 arms = std::popcount((unsigned)t);
    const f32 weight = arms == 0 ? 6.0f : (arms == 1 ? 0.05f : (arms == 2 ? (t == 5 || t == 10 ? 3.0f : 1.5f) : (arms == 3 ? 0.4f : 0.15f)));
    wfc_add_tile(rules, t, weight);
  }
  // Adjacency rule: two neighboring tiles must match at the edge, meaning both have a road or both have none on the side where they touch.
  for (i32 a = 0; a < 16; a++)
    for (i32 b = 0; b < 16; b++) {
      if (((a & right) != 0) == ((b & left) != 0))
        wfc_allow(rules, a, wfc_right, b);
      if (((a & down) != 0) == ((b & up) != 0))
        wfc_allow(rules, a, wfc_down, b);
    }

  wfc_desc d{.width = 40, .height = 14, .seed = 6};
  // Roads do not run off the map.
  d.allowed = [&](i32 x, i32 y, i32 tile) {
    return !((x == 0 && (tile & left)) || (x == d.width - 1 && (tile & right)) || (y == 0 && (tile & up)) ||
             (y == d.height - 1 && (tile & down)));
  };

  tile_grid out;
  if (!wfc_generate(rules, d, out)) {
    std::printf("could not generate\n");
    return 1;
  }
  const char *glyph[16] = {" ", "╵", "╶", "└", "╷", "│", "┌", "├", "╴", "┘", "─", "┴", "┐", "┤", "┬", "┼"};
  for (i32 y = 0; y < out.height; y++) {
    for (i32 x = 0; x < out.width; x++)
      std::printf("%s", glyph[out.get(x, y)]);
    std::putchar('\n');
  }
}
