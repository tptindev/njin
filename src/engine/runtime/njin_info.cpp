#include "njin_info.h"
#include "njin.h"
#include "njin2rl.h"
#include "rl2njin.h"
#include <raylib.h>

void njin::get_fps(const njin_ctx &ctx, f32 &fps) { fps = ctx.cfg.target_fps; }
void njin::get_delta(const njin_ctx &ctx, f32 &delta) { delta = ctx.dt; }
void njin::get_screen_size(const njin_ctx &ctx, vec2 &screen_size) {
  screen_size.x = ctx.cfg.width;
  screen_size.y = ctx.cfg.height;
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
