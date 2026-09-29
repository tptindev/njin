#include <njin.h>
#include <vector>

namespace {
struct zombie {
  njin::vec2 velocity{};
};

entt::entity hero = entt::null;
njin::spatial_index index; // kept across frames to reuse its memory
std::vector<entt::entity> who; // item i of the index is entity who[i]
std::vector<njin::spatial_item> items;
std::vector<njin::vec2> push;
entt::entity aimed = entt::null; // the zombie the player aims at

void startup(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  njin::rng &r = njin::random(ctx);
  hero = reg.create();
  reg.emplace<njin::transform>(hero, njin::transform{.pos = {640, 360}});
  for (int i = 0; i < 2000; i++) {
    const entt::entity z = reg.create();
    reg.emplace<njin::transform>(z, njin::transform{.pos = {r.range(0.0f, 1280.0f), r.range(0.0f, 720.0f)}});
    reg.emplace<zombie>(z);
  }
}

void update(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  const njin::vec2 target = reg.get<njin::transform>(hero).pos;
  const njin::f32 dt = njin::delta(ctx);

  // The swarm walks toward the player, then the index is rebuilt from the new positions.
  who.clear();
  items.clear();
  for (auto [e, tr, z] : reg.view<njin::transform, zombie>().each()) {
    z.velocity = njin::normalize(target - tr.pos) * 40.0f;
    tr.pos += z.velocity * dt;
    who.push_back(e);
    items.push_back({.pos = tr.pos, .radius = 6.0f});
  }
  // Zombies spread over one screen: the grid is the fastest choice.
  njin::spatial_build(index, {.kind = njin::spatial_grid}, items);

  // No overlapping: each is pushed only by the 6 nearest touching it.
  njin::spatial_separate(index, push, 6);
  for (std::size_t i = 0; i < who.size(); i++)
    reg.get<njin::transform>(who[i]).pos += push[i];

  // Auto-aim: the zombie nearest the player within 200.
  njin::spatial_hit nearest[1];
  aimed = njin::spatial_nearest(index, {.at = target, .radius = 200.0f}, nearest, 1) == 1 ? who[nearest[0].item]
                                                                                         : entt::null;
}

void draw(njin::njin_ctx &ctx) {
  const entt::registry &reg = njin::world(ctx);
  for (auto [e, tr, z] : reg.view<const njin::transform, const zombie>().each())
    njin::draw_circle(ctx, tr.pos, 6.0f, {0.4f, 0.7f, 0.3f, 1});
  const njin::vec2 me = reg.get<njin::transform>(hero).pos;
  if (reg.valid(aimed))
    njin::draw_line(ctx, me, reg.get<njin::transform>(aimed).pos, 1.0f, {1, 0.3f, 0.2f, 1});
  njin::draw_circle(ctx, me, 8.0f, {0.2f, 0.4f, 0.9f, 1});
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup, "startup");
  njin::ecs_register(ctx, njin::phase_update, update, "zombies");
  njin::ecs_register(ctx, njin::phase_render, draw, "draw");
}
} // namespace

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "spatial", .width = 1280, .height = 720});
  njin::njin_mod_register(*ctx, {.name = "spatial", .setup = setup});
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}
