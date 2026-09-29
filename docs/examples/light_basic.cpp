#include <njin.h>

namespace {
using namespace njin;

void load(context &ctx) {
  entt::registry &reg = world(ctx);

  // Bật ánh sáng. Ambient là ánh sáng có ở mọi nơi: ban đêm, một xanh sẫm.
  lighting_set(ctx, {.enabled = true, .ambient = {0.20f, 0.24f, 0.40f, 1.0f}});

  // Một đuốc: entity có transform và light_2d. Nhiệt độ màu 2400 K là lửa.
  const entt::entity torch = reg.create();
  reg.emplace<transform>(torch, transform{.pos = {320.0f, 200.0f}});
  reg.emplace<light_2d>(torch, light_2d{.temperature = 2400.0f, .intensity = 12.0f, .radius = 240.0f, .size = 30.0f});

  // Một cái cây: sprite với normal map và bản đồ vật liệu (nhám, kim loại, che khuất)...
  const entt::entity tree = reg.create();
  reg.emplace<transform>(tree, transform{.pos = {400.0f, 240.0f}});
  reg.emplace<sprite>(tree, sprite{.texture = texture_load(ctx, "assets/tree.png"),
                                   .origin = {0.5f, 1.0f},
                                   .normal = texture_load(ctx, "assets/tree_n.png"),
                                   .material = texture_load(ctx, "assets/tree_m.png")});
  // ...và một thân cây chắn sáng, để nó đổ bóng.
  reg.emplace<light_occluder>(tree, light_occluder_capsule({0.0f, -1.5f}, {0.0f, -5.0f}, 2.2f));

  // Một đèn pin: đèn nón chiếu sang phải (0 độ), mở 50 độ.
  const entt::entity flashlight = reg.create();
  reg.emplace<transform>(flashlight, transform{.pos = {100.0f, 220.0f}});
  reg.emplace<light_2d>(flashlight, light_2d{.kind = light_spot, .temperature = 5600.0f, .intensity = 16.0f,
                                             .radius = 360.0f, .size = 40.0f, .angle = 0.0f, .cone = 50.0f});

  // Một mặt trăng: đèn hướng, không có vị trí. Ánh sáng đi xuống-phải 35 độ, thấp trên đường chân trời.
  const entt::entity moon = reg.create();
  reg.emplace<transform>(moon);
  reg.emplace<light_2d>(moon, light_2d{.kind = light_directional, .temperature = 9000.0f, .intensity = 1.2f,
                                       .angle = 35.0f, .elevation = 30.0f});
}

void setup(context &ctx) { ecs_register(ctx, phase_startup, load, "load"); }
} // namespace

mod_desc light_basic_module() { return {.name = "light_basic", .setup = setup}; }
