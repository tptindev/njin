#include <njin.h>

namespace {
// Name the collision layers once, use them throughout the game.
constexpr njin::u32 layer_player = njin::layer_bit(0);
constexpr njin::u32 layer_wall = njin::layer_bit(1);
constexpr njin::u32 layer_enemy = njin::layer_bit(2);
constexpr njin::u32 layer_pickup = njin::layer_bit(3);
constexpr njin::u32 layer_player_shot = njin::layer_bit(4);

struct player {};
struct coin {};
struct bullet {
  njin::vec2 velocity{};
};
struct enemy {
  njin::i32 hp = 3;
};

entt::entity hero = entt::null;
njin::i32 score = 0;

// Each collision pair sends two events, one for each side: only look at `self`.
void on_enter(njin::context &ctx, njin::collision_enter &e) {
  entt::registry &reg = njin::world(ctx);
  // An earlier handler in the same frame may have destroyed one of the two.
  if (!reg.valid(e.self) || !reg.valid(e.other))
    return;
  if (reg.all_of<coin>(e.self) && reg.all_of<player>(e.other)) {
    score++;
    reg.destroy(e.self); // safe: events are sent after phase_post_update
  }
  if (reg.all_of<bullet>(e.self)) {
    if (enemy *foe = reg.try_get<enemy>(e.other); foe != nullptr && --foe->hp <= 0)
      reg.destroy(e.other);
    reg.destroy(e.self); // the bullet disappears when it hits an enemy or a wall
  }
}

void spawn(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  njin::events(ctx).sink<njin::collision_enter>().connect<&on_enter>(ctx);

  hero = reg.create();
  reg.emplace<player>(hero);
  reg.emplace<njin::transform>(hero, njin::transform{.pos = {100, 300}});
  reg.emplace<njin::collider>(hero, njin::collider{.size = {24, 32}, .layer = layer_player});

  // Wall: an ordinary obstacle.
  const entt::entity wall = reg.create();
  reg.emplace<njin::transform>(wall, njin::transform{.pos = {400, 300}});
  reg.emplace<njin::collider>(wall, njin::collider{.size = {32, 300}, .layer = layer_wall});

  // Coin: a trigger, only cares about the player.
  const entt::entity c = reg.create();
  reg.emplace<coin>(c);
  reg.emplace<njin::transform>(c, njin::transform{.pos = {250, 300}});
  reg.emplace<njin::collider>(c, njin::collider{.shape = njin::collider_circle, .radius = 8,
                                                .layer = layer_pickup, .mask = layer_player,
                                                .trigger = true});

  const entt::entity foe = reg.create();
  reg.emplace<enemy>(foe);
  reg.emplace<njin::transform>(foe, njin::transform{.pos = {600, 300}});
  reg.emplace<njin::collider>(foe, njin::collider{.size = {32, 32}, .layer = layer_enemy});

  njin::collision_set_debug(ctx, true); // draw collider outlines for debugging
}

void move_hero(njin::context &ctx) {
  njin::vec2 dir{};
  if (njin::key_held(ctx, njin::key_a)) dir.x -= 1;
  if (njin::key_held(ctx, njin::key_d)) dir.x += 1;
  if (njin::key_held(ctx, njin::key_w)) dir.y -= 1;
  if (njin::key_held(ctx, njin::key_s)) dir.y += 1;
  // Stops at the wall and slides along it; passes through the coin (a trigger).
  njin::collision_move(ctx, hero, njin::normalize(dir) * 200.0f * njin::delta(ctx));
}

void shoot(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  if (njin::key_pressed(ctx, njin::key_j)) {
    const entt::entity b = reg.create();
    reg.emplace<bullet>(b, bullet{.velocity = {600, 0}});
    reg.emplace<njin::transform>(b, reg.get<njin::transform>(hero));
    // Player bullet: only hits enemies and walls, never the shooter.
    reg.emplace<njin::collider>(b, njin::collider{.size = {6, 6}, .layer = layer_player_shot,
                                                  .mask = layer_enemy | layer_wall,
                                                  .trigger = true});
  }
  const njin::f32 dt = njin::delta(ctx);
  for (auto [e, b, tr] : reg.view<bullet, njin::transform>().each())
    tr.pos += b.velocity * dt;

  // Enemies only shoot when they see the player: a ray blocked by a wall means no sight.
  for (auto [e, foe, tr] : reg.view<enemy, njin::transform>().each()) {
    const njin::vec2 target = reg.get<njin::transform>(hero).pos;
    const njin::raycast_hit sight =
        njin::collision_raycast(ctx, tr.pos, target, layer_wall | layer_player, false, e);
    if (sight.hit && sight.entity == hero) {
      // ... the enemy shoots toward the player
    }
  }
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
  njin::ecs_register(ctx, njin::phase_fixed_update, move_hero);
  njin::ecs_register(ctx, njin::phase_update, shoot);
}
} // namespace

njin::mod_desc collision_example_module() { return {.name = "collision", .setup = setup}; }
