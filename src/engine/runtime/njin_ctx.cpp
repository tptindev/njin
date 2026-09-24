#include "njin_ctx.h"
#include "njin2rl.h"
#include "njin_input.h"
#include "rl2njin.h"
#include <raylib.h>

namespace njin {
void get_delta(const njin_ctx &ctx, f32 &delta) { delta = ctx.dt; }
void get_elapsed(const njin_ctx &ctx, f32 &time) { time = ctx.elapsed; }

void world_to_screen(const njin_ctx &ctx, vec2 world, vec2 &screen) {
  const Camera2D camera{};
  Vector2 position{};
  to_raylib(world, position);
  from_raylib(GetWorldToScreen2D(position, camera), screen);
}

void screen_to_world(const njin_ctx &ctx, vec2 screen, vec2 &world) {
  const Camera2D camera{};
  Vector2 position{};
  to_raylib(screen, position);
  from_raylib(GetScreenToWorld2D(position, camera), world);
}

bool key_pressed(const njin_ctx &ctx, key_code key) {
  return ctx.input != nullptr && input_key_pressed(*ctx.input, key);
}

bool key_held(const njin_ctx &ctx, key_code key) {
  return ctx.input != nullptr && input_key_held(*ctx.input, key);
}

bool key_released(const njin_ctx &ctx, key_code key) {
  return ctx.input != nullptr && input_key_released(*ctx.input, key);
}

action_handle action_register(njin_ctx &ctx, const char *name) {
  if (ctx.input == nullptr)
    return action_handle{};
  return input_action_register(*ctx.input, name);
}

action_handle action_find(const njin_ctx &ctx, const char *name) {
  if (ctx.input == nullptr)
    return action_handle{};
  return input_action_find(*ctx.input, name);
}

void action_bind_key(njin_ctx &ctx, action_handle handle, key_code key) {
  if (ctx.input != nullptr)
    input_action_bind_key(*ctx.input, handle, key);
}

bool action_pressed(const njin_ctx &ctx, action_handle handle) {
  return ctx.input != nullptr && input_action_pressed(*ctx.input, handle);
}

bool action_held(const njin_ctx &ctx, action_handle handle) {
  return ctx.input != nullptr && input_action_held(*ctx.input, handle);
}

bool action_released(const njin_ctx &ctx, action_handle handle) {
  return ctx.input != nullptr && input_action_released(*ctx.input, handle);
}
} // namespace njin
