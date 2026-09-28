#include <njin.h>

namespace {
njin::timer spawn_every{.duration = 2.0f, .repeat = true};
njin::timer invincible{.duration = 1.5f};
njin::tween<njin::vec2> slide_in{.from = {-200, 40},
                                 .to = {20, 40},
                                 .duration = 0.6f,
                                 .curve = njin::ease::out_back};
bool paused = false;

void update(njin::njin_ctx &ctx) {
  const njin::f32 dt = njin::delta(ctx); // 0 when paused

  if (spawn_every.tick(dt))
    NJIN_INFO("spawn a monster");

  // Pause: delta() drops to 0, phase_fixed_update stops, but systems still run,
  // so the key can still be read to unpause.
  if (njin::key_pressed(ctx, njin::key_p)) {
    paused = !paused;
    njin::time_set_paused(ctx, paused);
  }

  // Slow down while holding Shift: both delta() and the fixed phase slow down with it.
  const bool slow = njin::key_held(ctx, njin::key_left_shift);
  njin::time_set_scale(ctx, slow ? 0.3f : 1.0f);

  if (njin::key_pressed(ctx, njin::key_h))
    invincible.reset();
  invincible.tick(dt);
}

void draw_ui(njin::njin_ctx &ctx) {
  // A panel sliding onto the screen. Use delta_real so it still runs while paused.
  const njin::vec2 pos = slide_in.tick(njin::delta_real(ctx));
  njin::draw_text(ctx, paused ? "PAUSED" : "Playing", pos, 30,
                  njin::colors::white);

  // Blink while invincible: the timer's progress sets the frequency.
  if (!invincible.finished) {
    const bool on = (int)(invincible.progress() * 12.0f) % 2 == 0;
    if (on)
      njin::draw_text(ctx, "Invincible", {20, 80}, 24, njin::colors::yellow);
  }
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_update, update);
  njin::ecs_register(ctx, njin::phase_post_render, draw_ui);
}
} // namespace

njin::mod_desc time_module() { return {.name = "time", .setup = setup}; }
