#include <njin.h>

#include <cmath>
#include <vector>

namespace {
using namespace njin;

std::vector<entt::entity> shapes; // to draw their outline

// Một vật chắn: đặt light_occluder vào một entity có transform.
entt::entity place(njin_ctx &ctx, vec2 pos, light_occluder shape) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos});
  reg.emplace<light_occluder>(e, std::move(shape));
  shapes.push_back(e);
  return e;
}

void load(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);

  // Bật ánh sáng: ban đêm, trời xanh sẫm.
  lighting_set(ctx, {.enabled = true, .ambient = {0.34f, 0.38f, 0.55f, 1.0f}});

  // Một ngọn đèn ở giữa. Nhiệt độ màu 2700 K là bóng đèn dây tóc.
  const entt::entity lamp = reg.create();
  reg.emplace<transform>(lamp, transform{.pos = {400.0f, 300.0f}});
  reg.emplace<light_2d>(lamp, light_2d{.temperature = 2700.0f, .intensity = 14.0f, .radius = 380.0f, .size = 60.0f, .height = 90.0f});

  // Các hình vật chắn dựng sẵn, xếp thành vòng quanh đèn.
  place(ctx, {400.0f, 150.0f}, light_occluder_box({40.0f, 40.0f}));               // hình chữ nhật
  place(ctx, {545.0f, 205.0f}, light_occluder_circle(22.0f));                     // hình tròn
  place(ctx, {585.0f, 370.0f}, light_occluder_ellipse({34.0f, 14.0f}));           // elip
  place(ctx, {470.0f, 470.0f}, light_occluder_capsule({-20.0f, 0.0f}, {20.0f, 0.0f}, 9.0f)); // viên thuốc
  place(ctx, {230.0f, 445.0f},
        light_occluder_line({{-40.0f, 20.0f}, {0.0f, -25.0f}, {45.0f, 10.0f}})); // tường mỏng, gấp khúc
  place(ctx, {215.0f, 235.0f},
        light_occluder{{{-25.0f, 25.0f}, {0.0f, -30.0f}, {28.0f, 22.0f}, {5.0f, 8.0f}}}); // đa giác lõm, tự chỉ điểm

  // Theo hình sprite: bóng đúng hình cái cây. Nó cũng đổi theo frame animation và khi lật.
  const entt::entity tree = reg.create();
  reg.emplace<transform>(tree, transform{.pos = {320.0f, 380.0f}, .scale = 3.0f});
  reg.emplace<sprite>(tree, sprite{.texture = texture_load(ctx, "assets/sprites/tree.png"), .origin = {0.5f, 1.0f}});
  reg.emplace<light_occluder_sprite>(tree, light_occluder_sprite{.alpha = 0.5f, .simplify = 0.8f});
}

// Vẽ đường viền của các hình cho dễ thấy. Ánh sáng không tự vẽ vật chắn.
void outline(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  for (const entt::entity e : shapes) {
    const transform &tr = reg.get<transform>(e);
    const light_occluder &o = reg.get<light_occluder>(e);
    for (usize i = 0; i + (o.closed ? 0 : 1) < o.points.size(); i++) {
      const vec2 a = o.points[i], b = o.points[(i + 1) % o.points.size()];
      draw_line(ctx, tr.pos + a, tr.pos + b, 2.0f, {0.85f, 0.85f, 0.9f, 1.0f});
    }
  }
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_render, outline, "outline");
}
} // namespace

mod_desc light_shapes_module() { return {.name = "light_shapes", .setup = setup}; }
