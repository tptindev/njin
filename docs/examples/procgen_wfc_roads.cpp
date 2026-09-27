#include <bit>
#include <cstdio>
#include <njin.h>

using namespace njin;

// 16 ô đường, đánh số theo đúng bộ autotile_edges: bit 1 là đường đi lên, 2 sang phải, 4 xuống, 8 sang trái.
// Ô 0 là cỏ (không có đường), ô 5 là đường thẳng đứng, ô 10 là đường nằm ngang, ô 15 là ngã tư.
constexpr i32 up = 1, right = 2, down = 4, left = 8;

int main() {
  wfc_rules rules;
  // Độ hay gặp: nhiều cỏ và đường thẳng, ít ngã ba, gần như không ngã tư hay đường cụt.
  for (i32 t = 0; t < 16; t++) {
    const i32 arms = std::popcount((unsigned)t);
    const f32 weight = arms == 0 ? 6.0f : (arms == 1 ? 0.05f : (arms == 2 ? (t == 5 || t == 10 ? 3.0f : 1.5f) : (arms == 3 ? 0.4f : 0.15f)));
    wfc_add_tile(rules, t, weight);
  }
  // Luật kề: hai ô cạnh nhau phải khớp mép, tức là cùng có đường hoặc cùng không có ở phía chạm nhau.
  for (i32 a = 0; a < 16; a++)
    for (i32 b = 0; b < 16; b++) {
      if (((a & right) != 0) == ((b & left) != 0))
        wfc_allow(rules, a, wfc_right, b);
      if (((a & down) != 0) == ((b & up) != 0))
        wfc_allow(rules, a, wfc_down, b);
    }

  wfc_desc d{.width = 40, .height = 14, .seed = 6};
  // Đường không chạy ra khỏi bản đồ.
  d.allowed = [&](i32 x, i32 y, i32 tile) {
    return !((x == 0 && (tile & left)) || (x == d.width - 1 && (tile & right)) || (y == 0 && (tile & up)) ||
             (y == d.height - 1 && (tile & down)));
  };

  tile_grid out;
  if (!wfc_generate(rules, d, out)) {
    std::printf("không sinh được\n");
    return 1;
  }
  const char *glyph[16] = {" ", "╵", "╶", "└", "╷", "│", "┌", "├", "╴", "┘", "─", "┴", "┐", "┤", "┬", "┼"};
  for (i32 y = 0; y < out.height; y++) {
    for (i32 x = 0; x < out.width; x++)
      std::printf("%s", glyph[out.get(x, y)]);
    std::putchar('\n');
  }
}
