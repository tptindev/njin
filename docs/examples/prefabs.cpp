#include <njin.h>

namespace {
struct health {
  njin::i32 hp = 3;
};
struct enemy {};
struct orbit {
  njin::f32 speed = 180.0f; // độ mỗi giây
};

njin::texture_handle atlas{};
njin::prefab_handle sword{};

// Hàm dựng: entity đã có transform (vị trí spawn) và scene_owned.
void build_sword(njin::njin_ctx &ctx, entt::entity e) {
  njin::world(ctx).emplace<njin::sprite>(
      e, njin::sprite{.texture = atlas, .source = {{64, 0}, {16, 32}}, .layer = 11});
}

void build_goblin(njin::njin_ctx &ctx, entt::entity e) {
  entt::registry &reg = njin::world(ctx);
  reg.emplace<enemy>(e);
  reg.emplace<health>(e, health{.hp = 3});
  reg.emplace<njin::sprite>(e, njin::sprite{.texture = atlas,
                                            .source = {{0, 0}, {32, 32}},
                                            .layer = 10});
  // Kiếm là con của goblin: đi theo, xoay theo, và bị hủy cùng goblin.
  const entt::entity blade = njin::prefab_spawn_child(ctx, sword, e, {.pos = {14, 2}});
  reg.emplace<orbit>(blade);
}

void startup(njin::njin_ctx &ctx) {
  atlas = njin::texture_load(ctx, "assets/atlas.png");
  sword = njin::prefab_register(ctx, {.name = "sword", .build = build_sword});
  njin::prefab_register(ctx, {.name = "goblin", .build = build_goblin});

  // Tìm theo tên: tiện khi tên đọc từ file màn chơi.
  for (njin::i32 i = 0; i < 5; i++)
    njin::prefab_spawn(ctx, "goblin", {.pos = {100.0f + 120.0f * (njin::f32)i, 300}});
}

// Xoay kiếm quanh tay: sửa `local` của child_of, không sửa transform.
void spin(njin::njin_ctx &ctx) {
  const njin::f32 dt = njin::delta(ctx);
  for (auto [e, link, o] : njin::world(ctx).view<njin::child_of, orbit>().each())
    link.local.rot += o.speed * dt;
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_update, spin);
}
} // namespace

njin::mod_desc prefabs_module() { return {.name = "prefabs", .setup = setup}; }
