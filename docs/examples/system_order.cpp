#include <njin.h>

namespace {
void read_input(njin::njin_ctx &) {}
void move(njin::njin_ctx &) {}
void clamp_to_screen(njin::njin_ctx &) {}
void debug_overlay(njin::njin_ctx &) {}

void setup(njin::njin_ctx &ctx) {
  // Không ràng buộc: chạy theo thứ tự đăng ký (order mặc định là 100).
  njin::ecs_register(ctx, njin::phase_update, read_input);

  // after: move phải chạy sau read_input.
  njin::ecs_register(
      ctx, njin::phase_update,
      njin::sys_desc{.fnc = move, .after = {read_input}});

  // clamp_to_screen phải chạy sau move.
  njin::ecs_register(
      ctx, njin::phase_update,
      njin::sys_desc{.fnc = clamp_to_screen, .after = {move}});

  // order lớn hơn: chạy muộn nhất trong những system còn lại.
  njin::ecs_register(
      ctx, njin::phase_update,
      njin::sys_desc{.fnc = debug_overlay, .order = 200});
}
} // namespace

njin::mod_desc physics_module() {
  return {.name = "physics", .setup = setup};
}
