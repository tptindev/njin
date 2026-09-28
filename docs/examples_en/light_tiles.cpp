#include <njin.h>

namespace {
using namespace njin;

constexpr i32 tile_wall = 3; // index of the wall tile in the tileset

// Make a tilemap's walls block light. Call again when the walls change (digging, opening a door).
void wall_shadows(njin_ctx &ctx, entt::entity map_entity) {
  entt::registry &reg = world(ctx);
  const tilemap &map = reg.get<tilemap>(map_entity);
  const transform &at = reg.get<transform>(map_entity);

  // Each connected wall block and each hole in it is one occluder; collinear edges are merged.
  for (light_occluder &shape : light_occluders_from_tiles(map, [](i32 tile) { return tile == tile_wall; })) {
    const entt::entity e = reg.create();
    reg.emplace<transform>(e, at); // same origin and scale as the tilemap
    reg.emplace<light_occluder>(e, std::move(shape));
  }
}
} // namespace

void light_tiles_example(njin::njin_ctx &ctx, entt::entity map_entity) { wall_shadows(ctx, map_entity); }
