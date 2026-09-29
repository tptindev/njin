#include <njin.h>

namespace {
// Tag do game tự định nghĩa để tìm người chơi.
struct player_tag {};

void spawn(njin::context &ctx) {
  entt::registry &registry = njin::world(ctx);
  const njin::vec2 screen = njin::screen_size(ctx);

  const entt::entity player = registry.create();
  registry.emplace<njin::transform>(player);
  registry.emplace<player_tag>(player);

  // Camera là một entity có ba component: transform, camera_2d, camera_on.
  const entt::entity camera = registry.create();
  registry.emplace<njin::transform>(camera);
  registry.emplace<njin::camera_2d>(
      camera, njin::camera_2d{.offset = {screen.x * 0.5f, screen.y * 0.5f},
                              .zoom = 2.0f});
  registry.emplace<njin::camera_on>(camera);
}

// Đưa camera tới chỗ người chơi mỗi frame.
void follow_player(njin::context &ctx) {
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

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
  njin::ecs_register(ctx, njin::phase_post_update, follow_player);
}
} // namespace

njin::mod_desc camera_demo_module() {
  return {.name = "camera_demo", .setup = setup};
}

// Đổi tọa độ: điểm trong thế giới nằm ở đâu trên màn hình sau khi qua camera.
njin::vec2 world_to_screen_example(const njin::context &ctx,
                                   njin::vec2 world_pos) {
  return njin::w2scr(ctx, world_pos);
}
