#include <njin.h>

// Timers and tweens attached to an entity: no need to keep your own counter.
void on_pickup(njin::njin_ctx &ctx, entt::entity coin, entt::entity player) {
  entt::registry &reg = njin::world(ctx);
  const njin::vec2 pos = reg.get<njin::transform>(coin).pos;

  // Float up then fade out, then destroy itself when done.
  njin::tween_move(ctx, coin, pos + njin::vec2{0.0f, -18.0f}, 0.35f, njin::ease::out_cubic);
  njin::tween_tint(ctx, coin, {1, 1, 1, 0}, 0.35f, njin::ease::linear,
                   {.done = [coin](njin::njin_ctx &c) {
                     if (njin::world(c).valid(coin))
                       njin::world(c).destroy(coin);
                   }});

  // After 1.5 seconds reload the level. If the player is destroyed, the timer is destroyed with it.
  njin::timer_after(ctx, 1.5f, [](njin::njin_ctx &c) { njin::scene_fade(c, njin::scene_find(c, "play")); },
                    {.owner = player});

  // An arbitrary value: a health bar draining gradually.
  njin::tween_value(ctx, 1.0f, 0.4f, 0.5f, [](njin::njin_ctx &, njin::f32 value) { (void)value; },
                    njin::ease::out_quad);
}

// Repeating beat: spawn a monster every 3 seconds, exactly 5 times.
void start_wave(njin::njin_ctx &ctx) {
  njin::timer_every(ctx, 3.0f, [](njin::njin_ctx &c) { (void)c; }, 5);
}

// A button pops up then shrinks back: a tween that goes back and forth (yoyo).
void pulse(njin::njin_ctx &ctx, entt::entity button) {
  njin::tween_scale(ctx, button, 1.15f, 0.12f, njin::ease::out_quad, {.repeat = 1, .yoyo = true});
}

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "Timers", .width = 640, .height = 360, .target_fps = 60});
  (void)on_pickup;
  (void)start_wave;
  (void)pulse;
  njin::njin_destroy(ctx);
}
