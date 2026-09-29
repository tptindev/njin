#include <njin.h>

namespace {
njin::action_handle move_left;
njin::action_handle move_right;

void bind_keys(njin::context &ctx) {
  // Một action có thể có nhiều phím.
  move_left = njin::action_register(ctx, "move_left");
  njin::action_bind_key(ctx, move_left, njin::key_a);
  njin::action_bind_key(ctx, move_left, njin::key_left);

  move_right = njin::action_register(ctx, "move_right");
  njin::action_bind_key(ctx, move_right, njin::key_d);
  njin::action_bind_key(ctx, move_right, njin::key_right);
}

// Phím đang được ấn = vừa nhấn xuống (pressed) hoặc đã giữ từ frame trước (held).
bool is_down(const njin::context &ctx, njin::action_handle action) {
  return njin::action_pressed(ctx, action) || njin::action_held(ctx, action);
}

void steer(njin::context &ctx) {
  njin::f32 direction = 0.0f;
  if (is_down(ctx, move_left))
    direction -= 1.0f;
  if (is_down(ctx, move_right))
    direction += 1.0f;

  if (njin::key_pressed(ctx, njin::key_space))
    NJIN_INFO("nhảy! hướng hiện tại: %.0f", direction);
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, bind_keys);
  njin::ecs_register(ctx, njin::phase_update, steer);
}
} // namespace

njin::mod_desc input_demo_module() {
  return {.name = "input_demo", .setup = setup};
}
