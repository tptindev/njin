#include <njin.h>

namespace {
entt::entity player = entt::null;

void spawn(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);

  // Một đống lửa phát liên tục, khói bay lên phía trên.
  const entt::entity campfire = reg.create();
  reg.emplace<njin::transform>(campfire, njin::transform{.pos = {400, 300}});
  reg.emplace<njin::particle_emitter>(campfire, njin::fx::fire());

  const entt::entity smoke = reg.create();
  reg.emplace<njin::transform>(smoke, njin::transform{.pos = {400, 260}});
  reg.emplace<njin::particle_emitter>(smoke, njin::fx::smoke());

  // Emitter tự làm: bong bóng nổi lên từ một vùng rộng.
  njin::particle_emitter bubbles{};
  bubbles.rate = 8.0f;
  bubbles.area = {200.0f, 10.0f};
  bubbles.angle = -90.0f;
  bubbles.spread = 20.0f;
  bubbles.speed = {20.0f, 40.0f};
  bubbles.life = {2.0f, 3.0f};
  bubbles.size_start = 6.0f;
  bubbles.size_end = 10.0f;
  bubbles.color_start = {0.7f, 0.9f, 1.0f, 0.8f};
  bubbles.color_end = {0.7f, 0.9f, 1.0f, 0.0f};
  const entt::entity pond = reg.create();
  reg.emplace<njin::transform>(pond, njin::transform{.pos = {700, 400}});
  reg.emplace<njin::particle_emitter>(pond, bubbles);

  player = reg.create();
  reg.emplace<njin::transform>(player, njin::transform{.pos = {200, 300}});
}

// Gọi khi đòn đánh trúng kẻ địch tại `at`.
void on_hit(njin::njin_ctx &ctx, entt::entity enemy, njin::vec2 at) {
  njin::particle_emitter sparks = njin::fx::sparks();
  sparks.angle = -45.0f; // bắn chéo lên
  sparks.spread = 90.0f;
  njin::particles_spawn(ctx, sparks, at, 16);
  njin::sprite_flash(ctx, enemy);  // nháy trắng 0.1 giây
  njin::hitstop(ctx, 0.06f);       // dừng hình một chút cho "đã tay"
  njin::camera_shake(ctx, 0.35f);  // rung nhẹ
}

// Gọi khi thùng thuốc nổ phát nổ.
void on_explode(njin::njin_ctx &ctx, njin::vec2 at) {
  njin::particles_spawn(ctx, njin::fx::explosion(), at, 60);
  njin::particles_spawn(ctx, njin::fx::debris(), at, 12);
  njin::camera_shake(ctx, 0.8f);
  njin::screen_flash(ctx, {1.0f, 0.95f, 0.8f, 0.5f}, 0.25f);
}

void control(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  const njin::vec2 pos = reg.get<njin::transform>(player).pos;
  if (njin::key_pressed(ctx, njin::key_j))
    on_hit(ctx, player, pos + njin::vec2{30, 0});
  if (njin::key_pressed(ctx, njin::key_k))
    on_explode(ctx, pos + njin::vec2{150, 0});
  // Bụi dưới chân mỗi lần tiếp đất.
  if (njin::key_pressed(ctx, njin::key_l))
    njin::particles_spawn(ctx, njin::fx::dust(), pos + njin::vec2{0, 16}, 8);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
  njin::ecs_register(ctx, njin::phase_update, control);
}
} // namespace

njin::mod_desc particles_fx_module() { return {.name = "particles_fx", .setup = setup}; }
