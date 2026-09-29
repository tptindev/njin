#include <njin.h>

namespace {
struct player_state {
  njin::i32 hp = 10;
  njin::i32 max_hp = 10;
  bool paused = false;
  njin::f32 pause_blend = 0.0f; // 0 đang chơi, 1 menu pause hiện hẳn
};
player_state p;

void update(njin::context &ctx) {
  if (njin::key_pressed(ctx, njin::key_p)) {
    p.paused = !p.paused;
    njin::time_set_paused(ctx, p.paused);
  }
  // Chuyển mượt vào và ra menu pause, theo giờ thật vì game đang dừng.
  const njin::f32 step = njin::delta_real(ctx) * 4.0f;
  p.pause_blend = njin::move_toward(p.pause_blend, p.paused ? 1.0f : 0.0f, step);

  // Máu càng thấp, viền đỏ càng đậm.
  njin::post_fx hurt = njin::post::hurt();
  const njin::f32 lost = 1.0f - (njin::f32)p.hp / (njin::f32)p.max_hp;
  hurt.vignette *= lost;
  hurt.saturation = njin::lerp(1.0f, hurt.saturation, lost);

  njin::post_fx_set(ctx, njin::post_fx_lerp(hurt, njin::post::paused(), p.pause_blend));
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_update, update);
}
} // namespace

njin::mod_desc post_builtin_module() { return {.name = "post_builtin", .setup = setup}; }
