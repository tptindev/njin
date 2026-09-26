#include <njin.h>

// Hẹn giờ và tween gắn với entity: không cần tự giữ biến đếm.
void on_pickup(njin::njin_ctx &ctx, entt::entity coin, entt::entity player) {
  entt::registry &reg = njin::world(ctx);
  const njin::vec2 pos = reg.get<njin::transform>(coin).pos;

  // Bay lên rồi mờ đi, xong thì tự hủy.
  njin::tween_move(ctx, coin, pos + njin::vec2{0.0f, -18.0f}, 0.35f, njin::ease::out_cubic);
  njin::tween_tint(ctx, coin, {1, 1, 1, 0}, 0.35f, njin::ease::linear,
                   {.done = [coin](njin::njin_ctx &c) {
                     if (njin::world(c).valid(coin))
                       njin::world(c).destroy(coin);
                   }});

  // Sau 1.5 giây tải lại màn. Người chơi bị hủy thì hẹn giờ tự hủy theo.
  njin::timer_after(ctx, 1.5f, [](njin::njin_ctx &c) { njin::scene_fade(c, njin::scene_find(c, "play")); },
                    {.owner = player});

  // Một giá trị bất kỳ: thanh máu tụt từ từ.
  njin::tween_value(ctx, 1.0f, 0.4f, 0.5f, [](njin::njin_ctx &, njin::f32 value) { (void)value; },
                    njin::ease::out_quad);
}

// Nhịp lặp: sinh quái mỗi 3 giây, đúng 5 lần.
void start_wave(njin::njin_ctx &ctx) {
  njin::timer_every(ctx, 3.0f, [](njin::njin_ctx &c) { (void)c; }, 5);
}

// Nút bấm nảy lên rồi thu về: tween lặp qua lại (yoyo).
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
