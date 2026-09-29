#include <njin.h>

namespace {
using namespace njin;

constexpr i32 tile_wall = 3; // số thứ tự ô tường trong tileset

// Cho tường của một tilemap chắn sáng. Gọi lại khi tường đổi (đào tường, mở cửa).
void wall_shadows(context &ctx, entt::entity map_entity) {
  entt::registry &reg = world(ctx);
  const tilemap &map = reg.get<tilemap>(map_entity);
  const transform &at = reg.get<transform>(map_entity);

  // Mỗi khối tường liền và mỗi lỗ trong nó là một vật chắn; các cạnh thẳng hàng được nối lại.
  for (light_occluder &shape : light_occluders_from_tiles(map, [](i32 tile) { return tile == tile_wall; })) {
    const entt::entity e = reg.create();
    reg.emplace<transform>(e, at); // cùng gốc và tỉ lệ với tilemap
    reg.emplace<light_occluder>(e, std::move(shape));
  }
}
} // namespace

void light_tiles_example(njin::context &ctx, entt::entity map_entity) { wall_shadows(ctx, map_entity); }
