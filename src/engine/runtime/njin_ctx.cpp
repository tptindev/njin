#include "njin_ctx.h"
#include "njin_cfg.h"
#include "njin.h"
#include "njin2rl.h"
#include "rl2njin.h"
#include <raylib.h>

void njin::get_delta(const njin_ctx &ctx, f32 &delta) { delta = ctx.dt; }
void njin::get_elapsed(const njin_ctx &ctx, f32 &time) {
  time = ctx.elapsed; 
}
void njin::world_to_screen(const njin_ctx &ctx, vec2 world, vec2 &screen) {
  const Camera2D camera {};
  Vector2 position {};
  to_raylib(world, position);
  Vector2 ret = GetWorldToScreen2D(position, camera);
  from_raylib(ret, screen);
}
void njin::screen_to_world(const njin_ctx &ctx, vec2 screen, vec2 &world) {
  const Camera2D camera {};
  Vector2 position {};
  to_raylib(screen, position);
  Vector2 ret = GetScreenToWorld2D(position, camera);
  from_raylib(ret, world);
}

