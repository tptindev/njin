#include <cstdio>
#include <njin.h>

using namespace njin;

// Trong lưới, đất chỉ là một số "trên giấy". Bộ 47 ô bo góc của đất, giả sử, bắt đầu ở ô số 100 của tileset.
constexpr i32 ground = 1;

int main() {
  // Một hòn đảo chữ L, viết bằng chữ cho dễ nhìn. '#' là đất.
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

  // Ô nào nối liền ô nào: mỗi ô đất đổi thành số thứ tự trong bộ 47 ô, chọn theo tám ô kề.
  const tile_grid before = g;
  const i32 n = grid_autotile(g, {{.tiles = {ground}, .base = 100}});
  std::printf("%d ô đất được thay bằng ô của bộ\n\n", n);

  for (i32 y = 0; y < g.height; y++) {
    for (i32 x = 0; x < g.width; x++) {
      if (before.get(x, y) == -1)
        std::printf("  . ");
      else
        std::printf("%3d ", g.get(x, y) - 100);
    }
    std::putchar('\n');
  }

  // Cùng một mặt nạ, tính tay: ô (2, 1) là góc trên trái, chỉ có ô bên phải, bên dưới và chéo phải dưới.
  const u8 mask = neighbor_right | neighbor_down | neighbor_down_right;
  std::printf("\nô (2, 1): mặt nạ %d -> ô số %d của bộ (khớp: %s)\n", mask, autotile_index(mask),
              g.get(2, 1) == 100 + autotile_index(mask) ? "có" : "không");
}
