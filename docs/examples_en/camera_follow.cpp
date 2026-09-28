#include <njin.h>

namespace {
// Tag defined by the game itself to find the player.
struct player_tag {};

void spawn(njin::njin_ctx &ctx) {
  entt::registry &registry = njin::world(ctx);
  const njin::vec2 screen = njin::screen_size(ctx);

  const entt::entity player = registry.create();
  registry.emplace<njin::transform>(player);
  registry.emplace<player_tag>(player);

  // The camera is an entity with three components: transform, camera_2d, camera_on.
  const entt::entity camera = registry.create();
  registry.emplace<njin::transform>(camera);
  registry.emplace<njin::camera_2d>(
      camera, njin::camera_2d{.offset = {screen.x * 0.5f, screen.y * 0.5f},
                              .zoom = 2.0f});
  registry.emplace<njin::camera_on>(camera);
}

// Move the camera to the player every frame.
void follow_player(njin::njin_ctx &ctx) {
  entt::registry &registry = njin::world(ctx);
  const auto players = registry.view<const njin::transform, const player_tag>();
  const auto cameras = registry.view<njin::transform, const njin::camera_on>();

  for (const entt::entity player : players) {
    const njin::vec2 target = players.get<const njin::transform>(player).pos;
    for (const entt::entity camera : cameras) {
      cameras.get<njin::transform>(camera).pos = target;
    }
  }
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
  njin::ecs_register(ctx, njin::phase_post_update, follow_player);
}
} // namespace

njin::mod_desc camera_demo_module() {
  return {.name = "camera_demo", .setup = setup};
}

// Coordinate conversion: where a world point ends up on screen after the camera.
njin::vec2 world_to_screen_example(const njin::njin_ctx &ctx,
                                   njin::vec2 world_pos) {
  return njin::w2scr(ctx, world_pos);
}
