#include <njin.h>

namespace {
// Nhân vật dùng sheet xuất từ Aseprite với 4 tag: idle, run, jump, attack.
njin::anim_graph_handle hero_anim{};
entt::entity hero = entt::null;
bool grounded = true;
njin::f32 vel_y = 0.0f;

void spawn(njin::context &ctx) {
  const njin::anim_sheet_handle sheet = njin::anim_sheet_load(ctx, "assets/hero.json");
  hero_anim = njin::anim_graph_create(ctx, {
      .sheet = sheet,
      .states = {{.name = "idle", .clip = "idle"},
                 {.name = "run", .clip = "run"},
                 {.name = "jump", .clip = "jump", .repeat = 1}, // chạy một lượt rồi đứng ở frame cuối
                 {.name = "attack", .clip = "attack", .repeat = 1}},
      // Xét từ trên xuống, dùng cái đầu tiên thỏa mãn.
      .transitions = {
          {.to = "attack", .when = {{"attack", njin::anim_trigger}}},  // từ mọi trạng thái
          {.from = "attack", .to = "idle", .after_finish = true},     // đánh xong thì về idle
          {.from = "idle", .to = "jump", .when = {{"grounded", njin::anim_false}}},
          {.from = "run", .to = "jump", .when = {{"grounded", njin::anim_false}}},
          {.from = "jump", .to = "idle", .when = {{"grounded", njin::anim_true}}},
          {.from = "idle", .to = "run", .when = {{"speed", njin::anim_gt, 10}}},
          {.from = "run", .to = "idle", .when = {{"speed", njin::anim_le, 10}}},
      }});

  entt::registry &reg = njin::world(ctx);
  hero = reg.create();
  reg.emplace<njin::transform>(hero, njin::transform{.pos = {200, 300}, .scale = 3});
  reg.emplace<njin::sprite>(hero, njin::sprite{.origin = {0.5f, 1.0f}});
  reg.emplace<njin::animator>(hero, njin::animator{.graph = hero_anim});
}

void control(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  auto &tr = reg.get<njin::transform>(hero);
  auto &anim = reg.get<njin::animator>(hero);
  const njin::f32 dt = njin::delta(ctx);

  njin::f32 dir = 0;
  if (njin::key_held(ctx, njin::key_a))
    dir -= 1;
  if (njin::key_held(ctx, njin::key_d))
    dir += 1;
  if (grounded && njin::key_pressed(ctx, njin::key_space)) {
    vel_y = -420.0f;
    grounded = false;
  }
  vel_y += 1200.0f * dt;
  tr.pos += njin::vec2{dir * 160.0f, vel_y} * dt;
  if (tr.pos.y >= 300.0f) { // mặt đất
    tr.pos.y = 300.0f;
    vel_y = 0.0f;
    grounded = true;
  }
  if (dir != 0)
    reg.get<njin::sprite>(hero).flip_x = dir < 0;

  // Game chỉ báo tình trạng; graph tự chọn animation.
  njin::animator_set(ctx, anim, "speed", dir != 0 ? 160.0f : 0.0f);
  njin::animator_set_bool(ctx, anim, "grounded", grounded);
  if (njin::key_pressed(ctx, njin::key_j))
    njin::animator_trigger(ctx, anim, "attack");

  // Khóa di chuyển khi đang đánh.
  if (njin::animator_in(ctx, anim, "attack"))
    tr.pos.x -= dir * 160.0f * dt;
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
  njin::ecs_register(ctx, njin::phase_update, control);
}
} // namespace

njin::mod_desc animation_module() { return {.name = "animation", .setup = setup}; }
