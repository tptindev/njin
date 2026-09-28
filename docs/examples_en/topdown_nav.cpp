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

void startup(njin::njin_ctx &ctx) {
  // Everything standing on the map is drawn on the same layer, sorted by y: walk around behind a tree.
  njin::draw_set_y_sort(ctx, draw_things, true);

  g.level = njin::level_load(ctx, "assets/map.tmx");
  // The pathfinding grid is built from the colliders (walls, trees, ponds) already in the world.
  g.nav = njin::nav_grid_from_world(ctx, njin::level_bounds(ctx, g.level), {16.0f, 16.0f}, layer_world);
}

// A monster chases the player: A* every 0.4 seconds, then follows the path.
void chase(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  const njin::vec2 target = reg.get<njin::transform>(g.player).pos;
  for (auto [e, tr, c, body] : reg.view<njin::transform, chaser, njin::topdown_body>().each()) {
    c.repath -= njin::delta(ctx);
    // Only chase when the player is visible: the ray is not blocked by a wall.
    if (!njin::collision_line_of_sight(ctx, tr.pos, target, layer_world, e))
      continue;
    if (c.repath <= 0.0f) {
      c.repath = 0.4f;
      std::vector<njin::vec2> path;
      njin::nav_find_path(g.nav, tr.pos, target, path);
      c.agent.set(std::move(path));
    }
    // nav_steer returns the direction to move (length 1); topdown_body handles speed and collision.
    body.input.move = njin::nav_steer(c.agent, tr.pos);
  }
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_fixed_update, chase);
}
} // namespace

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "Top-down", .width = 1280, .height = 720, .target_fps = 60});
  njin::njin_mod_register(*ctx, {.name = "game", .setup = setup});
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}
