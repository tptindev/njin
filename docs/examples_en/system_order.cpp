#include <njin.h>

namespace {
void read_input(njin::context &) {}
void move(njin::context &) {}
void clamp_to_screen(njin::context &) {}
void debug_overlay(njin::context &) {}

void setup(njin::context &ctx) {
  // No constraint: runs in registration order (the default order is 100).
  njin::ecs_register(ctx, njin::phase_update, read_input);

  // after: move must run after read_input.
  njin::ecs_register(
      ctx, njin::phase_update,
      njin::sys_desc{.fnc = move, .after = {read_input}});

  // clamp_to_screen must run after move.
  njin::ecs_register(
      ctx, njin::phase_update,
      njin::sys_desc{.fnc = clamp_to_screen, .after = {move}});

  // Larger order: runs last among the remaining systems.
  njin::ecs_register(
      ctx, njin::phase_update,
      njin::sys_desc{.fnc = debug_overlay, .order = 200});
}
} // namespace

njin::mod_desc physics_module() {
  return {.name = "physics", .setup = setup};
}
