#include <njin.h>

#include <string>
#include <vector>

namespace {
// Danh sách để lọc, và chữ người chơi gõ vào ô tìm.
const std::vector<std::string> items = {"Kiếm gỗ", "Kiếm sắt", "Khiên", "Bình máu", "Bản đồ"};
std::string search;
bool bag_open = false;

void shop(njin::context &ctx) {
  njin::ui_begin(ctx, {.id = "shop", .title = "Cửa hàng", .width = 420});
  njin::ui_input(ctx, "Tìm##search", search, {.hint = "Gõ tên món đồ", .max_chars = 24});
  for (const std::string &item : items)
    if (search.empty() || item.find(search) != std::string::npos)
      njin::ui_label(ctx, item.c_str());
  njin::ui_end(ctx);
}

void update(njin::context &ctx) {
  // Trong lúc gõ, phím không tới game: chữ "i" gõ vào ô không mở túi đồ.
  if (njin::key_pressed(ctx, njin::key_i))
    bag_open = !bag_open;
  // Thứ không đi qua phím (tay cầm, action) thì hỏi ui_typing() trước.
  if (!njin::ui_typing(ctx) && njin::pad_pressed(ctx, 0, njin::pad_face_up))
    bag_open = !bag_open;
}
} // namespace

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_update, update, "update");
  njin::ecs_register(ctx, njin::phase_post_render, shop, "shop");
}
