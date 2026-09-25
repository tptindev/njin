#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include <entt/entity/registry.hpp>

namespace njin {
f32 delta(const njin_ctx &ctx) { return ctx.dt; }
f32 elapsed(const njin_ctx &ctx) { return ctx.elapsed; }
entt::registry &world(njin_ctx &ctx) { return ctx.ecs.registry; }
entt::dispatcher &events(njin_ctx &ctx) { return ctx.ecs.dispatcher; }
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

void action_bind_mouse(njin_ctx &ctx, action_handle handle,
                       mouse_button button) {
  input_action_bind_mouse(ctx.input, handle, button);
}

void action_bind_pad(njin_ctx &ctx, action_handle handle,
                     gamepad_button button) {
  input_action_bind_pad(ctx.input, handle, button);
}

void action_clear_binds(njin_ctx &ctx, action_handle handle) {
  input_action_clear_binds(ctx.input, handle);
}

vec2 mouse_pos(const njin_ctx &ctx) { return input_mouse_pos(ctx.input); }

vec2 mouse_delta(const njin_ctx &ctx) { return input_mouse_delta(ctx.input); }

f32 mouse_wheel(const njin_ctx &ctx) { return input_mouse_wheel(ctx.input); }

bool mouse_pressed(const njin_ctx &ctx, mouse_button button) {
  return input_mouse_pressed(ctx.input, button);
}

bool mouse_held(const njin_ctx &ctx, mouse_button button) {
  return input_mouse_held(ctx.input, button);
}

bool mouse_released(const njin_ctx &ctx, mouse_button button) {
  return input_mouse_released(ctx.input, button);
}

bool pad_available(const njin_ctx &ctx, i32 pad) {
  return input_pad_available(ctx.input, pad);
}

bool pad_pressed(const njin_ctx &ctx, i32 pad, gamepad_button button) {
  return input_pad_pressed(ctx.input, pad, button);
}

bool pad_held(const njin_ctx &ctx, i32 pad, gamepad_button button) {
  return input_pad_held(ctx.input, pad, button);
}

bool pad_released(const njin_ctx &ctx, i32 pad, gamepad_button button) {
  return input_pad_released(ctx.input, pad, button);
}

f32 pad_axis(const njin_ctx &ctx, i32 pad, gamepad_axis axis) {
  return input_pad_axis(ctx.input, pad, axis);
}

void pad_set_deadzone(njin_ctx &ctx, f32 deadzone) {
  input_pad_set_deadzone(ctx.input, deadzone);
}

i32 text_count(const njin_ctx &ctx) { return input_text_count(ctx.input); }

i32 text_char(const njin_ctx &ctx, i32 index) {
  return input_text_char(ctx.input, index);
}

void key_consume(njin_ctx &ctx, key_code key) {
  input_key_consume(ctx.input, key);
}

void mouse_consume(njin_ctx &ctx, mouse_button button) {
  input_mouse_consume(ctx.input, button);
}

void mouse_wheel_consume(njin_ctx &ctx) {
  input_mouse_wheel_consume(ctx.input);
}

axis_handle axis_register(njin_ctx &ctx, const char *name) {
  return input_axis_register(ctx.input, name);
}

axis_handle axis_find(const njin_ctx &ctx, const char *name) {
  return input_axis_find(ctx.input, name);
}

void axis_bind_keys(njin_ctx &ctx, axis_handle handle, key_code negative,
                    key_code positive) {
  input_axis_bind_keys(ctx.input, handle, negative, positive);
}

void axis_bind_pad(njin_ctx &ctx, axis_handle handle, gamepad_axis axis) {
  input_axis_bind_pad(ctx.input, handle, axis);
}

void axis_clear_binds(njin_ctx &ctx, axis_handle handle) {
  input_axis_clear_binds(ctx.input, handle);
}

f32 axis_value(const njin_ctx &ctx, axis_handle handle) {
  return input_axis_value(ctx.input, handle);
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

texture_handle texture_load(njin_ctx &ctx, const char *path) {
  return texture_store_load(ctx.texture, path);
}

void texture_unload(njin_ctx &ctx, texture_handle handle) {
  texture_store_unload(ctx.texture, handle);
}

vec2 texture_size(const njin_ctx &ctx, texture_handle handle) {
  return texture_store_size(ctx.texture, handle);
}

void texture_draw(const njin_ctx &ctx, texture_handle handle, vec2 pos,
                  rgba tint) {
  texture_store_draw(ctx.texture, handle, pos, tint);
}

render_texture_handle render_texture_load(njin_ctx &ctx, u32 width,
                                          u32 height) {
  return render_texture_store_load(ctx.render_texture, width, height);
}

void render_texture_unload(njin_ctx &ctx, render_texture_handle handle) {
  render_texture_store_unload(ctx.render_texture, handle);
}

vec2 render_texture_size(const njin_ctx &ctx, render_texture_handle handle) {
  return render_texture_store_size(ctx.render_texture, handle);
}

void render_texture_begin(const njin_ctx &ctx, render_texture_handle handle) {
  render_texture_store_begin(ctx.render_texture, handle);
}

void render_texture_begin(const njin_ctx &ctx, render_texture_handle handle,
                          rgba clear) {
  if (render_texture_store_begin(ctx.render_texture, handle))
    render_texture_store_clear(clear);
}

void render_texture_end(const njin_ctx &) { render_texture_store_end(); }

void render_texture_draw(const njin_ctx &ctx, render_texture_handle handle,
                         vec2 pos, rgba tint) {
  render_texture_store_draw(ctx.render_texture, handle, pos, tint);
}
} // namespace njin
