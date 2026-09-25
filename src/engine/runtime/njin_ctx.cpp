#include "njin_ctx.h"
#include "njin2rl.h"
#include "njin_ctx_impl.h"
#include "rl2njin.h"
#include <entt/entity/registry.hpp>
#include <raylib.h>

namespace njin {
f32 delta(const njin_ctx &ctx) { return ctx.dt; }
f32 elapsed(const njin_ctx &ctx) { return ctx.elapsed; }
entt::registry &world(njin_ctx &ctx) { return ctx.registry; }
entt::dispatcher &events(njin_ctx &ctx) { return ctx.dispatcher; }
void world_to_screen(const njin_ctx &ctx, vec2 world, vec2 &screen) {
  (void)ctx;
  const Camera2D camera{};
  Vector2 position{};
  to_raylib(world, position);
  from_raylib(GetWorldToScreen2D(position, camera), screen);
}

void screen_to_world(const njin_ctx &ctx, vec2 screen, vec2 &world) {
  (void)ctx;
  const Camera2D camera{};
  Vector2 position{};
  to_raylib(screen, position);
  from_raylib(GetScreenToWorld2D(position, camera), world);
}

bool key_pressed(const njin_ctx &ctx, key_code key) {
  return input_key_pressed(ctx.input, key);
}

bool key_held(const njin_ctx &ctx, key_code key) {
  return input_key_held(ctx.input, key);
}

bool key_released(const njin_ctx &ctx, key_code key) {
  return input_key_released(ctx.input, key);
}

action_handle action_register(njin_ctx &ctx, const char *name) {
  return input_action_register(ctx.input, name);
}

action_handle action_find(const njin_ctx &ctx, const char *name) {
  return input_action_find(ctx.input, name);
}

void action_bind_key(njin_ctx &ctx, action_handle handle, key_code key) {
  input_action_bind_key(ctx.input, handle, key);
}

bool action_pressed(const njin_ctx &ctx, action_handle handle) {
  return input_action_pressed(ctx.input, handle);
}

bool action_held(const njin_ctx &ctx, action_handle handle) {
  return input_action_held(ctx.input, handle);
}

bool action_released(const njin_ctx &ctx, action_handle handle) {
  return input_action_released(ctx.input, handle);
}

shader_handle shader_load(njin_ctx &ctx, const char *vspath,
                          const char *fspath) {
  return shader_store_load(ctx.shader, vspath, fspath);
}

void shader_unload(njin_ctx &ctx, shader_handle handle) {
  shader_store_unload(ctx.shader, handle);
}

void shader_begin(const njin_ctx &ctx, shader_handle handle) {
  shader_store_begin(ctx.shader, handle);
}

void shader_end(const njin_ctx &) { shader_store_end(); }

void shader_set_i32(njin_ctx &ctx, shader_handle handle, const char *name,
                    i32 value) {
  shader_store_set_i32(ctx.shader, handle, name, value);
}

void shader_set_f32(njin_ctx &ctx, shader_handle handle, const char *name,
                    f32 value) {
  shader_store_set_f32(ctx.shader, handle, name, value);
}

void shader_set_vec2(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec2 value) {
  shader_store_set_vec2(ctx.shader, handle, name, value);
}

void shader_set_vec4(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec4 value) {
  shader_store_set_rgba(ctx.shader, handle, name, value);
}
} // namespace njin
