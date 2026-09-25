#include <njin.h>

namespace {
njin::axis_handle move_x;
njin::axis_handle move_y;
njin::action_handle fire;

void bind(njin::njin_ctx &ctx) {
  // Một axis có thể nhận cả cặp phím lẫn cần analog. Nguồn nào lệch xa vị trí
  // nghỉ nhất thì thắng.
  move_x = njin::axis_register(ctx, "move_x");
  njin::axis_bind_keys(ctx, move_x, njin::key_a, njin::key_d);
  njin::axis_bind_pad(ctx, move_x, njin::pad_axis_left_x);

  move_y = njin::axis_register(ctx, "move_y");
  njin::axis_bind_keys(ctx, move_y, njin::key_w, njin::key_s);
  njin::axis_bind_pad(ctx, move_y, njin::pad_axis_left_y);

  // Một action nhận cả phím, nút chuột và nút tay cầm.
  fire = njin::action_register(ctx, "fire");
  njin::action_bind_key(ctx, fire, njin::key_space);
  njin::action_bind_mouse(ctx, fire, njin::mouse_left);
  njin::action_bind_pad(ctx, fire, njin::pad_face_down);

  // Vùng chết của cần analog, mặc định 0.15.
  njin::pad_set_deadzone(ctx, 0.2f);
}

void steer(njin::njin_ctx &ctx) {
  // Hai axis đọc cùng nhau là một vector. Có chuẩn hóa hay không do bạn quyết.
  const njin::vec2 direction{njin::axis_value(ctx, move_x),
                             njin::axis_value(ctx, move_y)};
  (void)direction;

  if (njin::action_pressed(ctx, fire)) {
    // Chuột tính bằng pixel màn hình; scr2w đổi sang vị trí trong thế giới.
    const njin::vec2 target = njin::scr2w(ctx, njin::mouse_pos(ctx));
    NJIN_INFO("bắn tới %.0f, %.0f", target.x, target.y);
  }

  // Nút và cần của một tay cầm cụ thể.
  if (njin::pad_available(ctx, 0)) {
    if (njin::pad_pressed(ctx, 0, njin::pad_start))
      NJIN_INFO("Start");
    const njin::f32 trigger = njin::pad_axis(ctx, 0, njin::pad_axis_right_trigger);
    (void)trigger; // -1 khi nghỉ, 1 khi nhấn hết
  }

  // Bánh xe chuột: dương là cuộn ra xa người dùng.
  const njin::f32 wheel = njin::mouse_wheel(ctx);
  if (wheel != 0.0f)
    NJIN_INFO("cuộn %.1f nấc", wheel);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, bind);
  njin::ecs_register(ctx, njin::phase_update, steer);
}
} // namespace

njin::mod_desc input_devices_module() {
  return {.name = "input_devices", .setup = setup};
}
