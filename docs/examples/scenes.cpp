#include <njin.h>

namespace {
njin::scene_handle menu, play;

// Chạy một lần khi vào scene "play": tạo thế giới của màn chơi.
void enter_play(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  for (int i = 0; i < 10; i++) {
    const entt::entity e = reg.create();
    reg.emplace<njin::transform>(e, njin::transform{.pos = {i * 40.0f, 100}});
    // Rời scene "play" thì engine tự hủy entity này.
    reg.emplace<njin::scene_owned>(e, njin::scene_owned{play});
  }
}

void menu_update(njin::njin_ctx &ctx) {
  if (njin::key_pressed(ctx, njin::key_enter))
    njin::scene_set(ctx, play); // chuyển ở đầu frame sau
}

void play_update(njin::njin_ctx &ctx) {
  if (njin::key_pressed(ctx, njin::key_escape))
    njin::scene_set(ctx, menu);
}

void draw_menu(njin::njin_ctx &ctx) {
  njin::draw_text(ctx, "Press Enter", {20, 20}, 30, njin::colors::white);
}

void setup(njin::njin_ctx &ctx) {
  // Đăng ký scene trước, để dùng handle trong sys_desc.
  menu = njin::scene_register(ctx, {.name = "menu"});
  play = njin::scene_register(ctx, {.name = "play", .on_enter = enter_play});

  // Mỗi system chỉ chạy trong scene của nó.
  njin::ecs_register(ctx, njin::phase_update, njin::sys_desc{.fnc = menu_update, .scene = menu});
  njin::ecs_register(ctx, njin::phase_update, njin::sys_desc{.fnc = play_update, .scene = play});
  njin::ecs_register(ctx, njin::phase_post_render, njin::sys_desc{.fnc = draw_menu, .scene = menu});
  njin::ecs_register(ctx, njin::phase_startup,
                     [](njin::njin_ctx &c) { njin::scene_set(c, menu); });
}
} // namespace

njin::mod_desc scenes_module() { return {.name = "scenes", .setup = setup}; }
