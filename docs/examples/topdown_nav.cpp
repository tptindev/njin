#include <njin.h>
#include <vector>

namespace {
constexpr njin::u32 layer_world = njin::layer_bit(0);
constexpr njin::i32 draw_things = 5;

struct chaser {
  njin::nav_agent agent;
  float repath = 0.0f;
};

struct game {
  njin::nav_grid nav;
  njin::level_handle level;
  entt::entity player = entt::null;
} g;

void startup(njin::context &ctx) {
  // Mọi thứ đứng trên bản đồ vẽ cùng một lớp, sắp theo y: đi vòng ra sau cây.
  njin::draw_set_y_sort(ctx, draw_things, true);

  g.level = njin::level_load(ctx, "assets/map.tmx");
  // Lưới tìm đường dựng từ các collider (tường, cây, ao) đang có trong thế giới.
  g.nav = njin::nav_grid_from_world(ctx, njin::level_bounds(ctx, g.level), {16.0f, 16.0f}, layer_world);
}

// Quái đuổi người chơi: A* mỗi 0.4 giây, rồi đi theo đường.
void chase(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  const njin::vec2 target = reg.get<njin::transform>(g.player).pos;
  for (auto [e, tr, c, body] : reg.view<njin::transform, chaser, njin::topdown_body>().each()) {
    c.repath -= njin::delta(ctx);
    // Chỉ đuổi khi nhìn thấy người chơi: tia không bị tường chắn.
    if (!njin::collision_line_of_sight(ctx, tr.pos, target, layer_world, e))
      continue;
    if (c.repath <= 0.0f) {
      c.repath = 0.4f;
      std::vector<njin::vec2> path;
      njin::nav_find_path(g.nav, tr.pos, target, path);
      c.agent.set(std::move(path));
    }
    // nav_steer trả hướng đi (độ dài 1); topdown_body lo tốc độ và va chạm.
    body.input.move = njin::nav_steer(c.agent, tr.pos);
  }
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_fixed_update, chase);
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "Top-down", .width = 1280, .height = 720, .target_fps = 60});
  njin::mod_register(*ctx, {.name = "game", .setup = setup});
  njin::run(*ctx);
  njin::destroy(ctx);
}
