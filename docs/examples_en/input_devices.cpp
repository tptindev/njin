#include <njin.h>

namespace {
njin::axis_handle move_x;
njin::axis_handle move_y;
njin::action_handle fire;

void bind(njin::njin_ctx &ctx) {
  // An axis can take both a pair of keys and an analog stick. Whichever source
  // is furthest from its resting position wins.
  move_x = njin::axis_register(ctx, "move_x");
  njin::axis_bind_keys(ctx, move_x, njin::key_a, njin::key_d);
  njin::axis_bind_pad(ctx, move_x, njin::pad_axis_left_x);

  move_y = njin::axis_register(ctx, "move_y");
  njin::axis_bind_keys(ctx, move_y, njin::key_w, njin::key_s);
  njin::axis_bind_pad(ctx, move_y, njin::pad_axis_left_y);

  // An action takes keys, mouse buttons and gamepad buttons.
  fire = njin::action_register(ctx, "fire");
  njin::action_bind_key(ctx, fire, njin::key_space);
  njin::action_bind_mouse(ctx, fire, njin::mouse_left);
  njin::action_bind_pad(ctx, fire, njin::pad_face_down);

  // Analog stick deadzone, 0.15 by default.
  njin::pad_set_deadzone(ctx, 0.2f);
}

void steer(njin::njin_ctx &ctx) {
  // Two axes read together make a vector. Whether to normalize it is up to you.
  const njin::vec2 direction{njin::axis_value(ctx, move_x),
                             njin::axis_value(ctx, move_y)};
  (void)direction;

  if (njin::action_pressed(ctx, fire)) {
    // The mouse is in screen pixels; scr2w converts to a world position.
    const njin::vec2 target = njin::scr2w(ctx, njin::mouse_pos(ctx));
    NJIN_INFO("shoot at %.0f, %.0f", target.x, target.y);
  }

  // Buttons and sticks of one specific gamepad.
  if (njin::pad_available(ctx, 0)) {
    if (njin::pad_pressed(ctx, 0, njin::pad_start))
      NJIN_INFO("Start");
    const njin::f32 trigger = njin::pad_axis(ctx, 0, njin::pad_axis_right_trigger);
    (void)trigger; // -1 at rest, 1 when fully pressed
  }

  // Mouse wheel: positive is scrolling away from the user.
  const njin::f32 wheel = njin::mouse_wheel(ctx);
  if (wheel != 0.0f)
    NJIN_INFO("scrolled %.1f notches", wheel);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, bind);
  njin::ecs_register(ctx, njin::phase_update, steer);
}
} // namespace

njin::mod_desc input_devices_module() {
  return {.name = "input_devices", .setup = setup};
}
