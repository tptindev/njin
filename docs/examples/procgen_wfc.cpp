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
  // 1. Mẫu: một khúc địa hình do người vẽ. WFC học từ nó ô nào được đứng cạnh ô nào theo từng hướng (trời chỉ
  //    nằm trên cỏ hoặc trên trời, cỏ nằm trên đất, đất nằm trên đá...), và ô nào hay gặp hơn ô nào.
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

  // 2. Sinh. Cùng seed cho cùng kết quả; đổi seed là có bản khác.
  wfc_desc d{.width = 60, .height = 20, .seed = 8};

  // 3. Ràng buộc: hàng trên cùng luôn là trời, hàng dưới cùng luôn là đá.
  d.allowed = [&](i32, i32 y, i32 tile) {
    if (y == 0)
      return tile == sky;
    if (y == d.height - 1)
      return tile == stone;
    return true;
  };

  tile_grid out;
  if (!wfc_generate(rules, d, out)) {
    std::printf("luật quá chặt hoặc ràng buộc mâu thuẫn\n");
    return 1;
  }
  print(out);
}
