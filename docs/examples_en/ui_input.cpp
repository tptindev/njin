#include <njin.h>

#include <string>
#include <vector>

namespace {
// The list to filter, and what the player types into the search field.
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
  // While typing, keys do not reach the game: an "i" typed into the field does not open the bag.
  if (njin::key_pressed(ctx, njin::key_i))
    bag_open = !bag_open;
  // Input that does not go through the keys (gamepad, actions): ask ui_typing() first.
  if (!njin::ui_typing(ctx) && njin::pad_pressed(ctx, 0, njin::pad_face_up))
    bag_open = !bag_open;
}
} // namespace

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_update, update, "update");
  njin::ecs_register(ctx, njin::phase_post_render, shop, "shop");
}
