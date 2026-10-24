#include "njin_ctx.h"
#include "njin_3d.h"
#include "njin_audio.h"
#include "njin_camera.h"
#include "njin_input.h"
#include "njin_render.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_audio_impl.h"
#include "njin_ecs.h"
#include "njin_input_impl.h"
#include "njin_shader.h"
#include "njin_texture.h"
#include "njin_view.h"
#include "njin_cfg.h"
#include "njin_log.h"
#include <algorithm>
#include <cstring>
#include <entt/entity/registry.hpp>

namespace njin {
f32 delta(const context &ctx) {
  return ctx_ref<time_state>(ctx).in_fixed ? ctx_ref<time_state>(ctx).fixed_dt : ctx_ref<time_state>(ctx).dt;
}
f32 delta_real(const context &ctx) { return ctx_ref<time_state>(ctx).dt_real; }
f32 elapsed(const context &ctx) { return ctx_ref<time_state>(ctx).elapsed; }

void time_set_scale(context &ctx, f32 scale) {
  ctx_ref<time_state>(ctx).scale = scale < 0.0f ? 0.0f : scale;
}
f32 time_scale(const context &ctx) { return ctx_ref<time_state>(ctx).scale; }
void time_set_paused(context &ctx, bool paused) { ctx_ref<time_state>(ctx).paused = paused; }
bool time_paused(const context &ctx) { return ctx_ref<time_state>(ctx).paused; }
f32 fixed_delta(const context &ctx) { return ctx_ref<time_state>(ctx).fixed_dt; }
f32 fixed_alpha(const context &ctx) { return ctx_ref<time_state>(ctx).fixed_alpha; }
void time_set_fixed_rate(context &ctx, f32 rate) {
  ctx_ref<time_state>(ctx).fixed_rate = rate >= 0.5f ? (rate <= 2.0f ? rate : 2.0f) : 0.5f; // NaN gives 0.5
}
f32 time_fixed_rate(const context &ctx) { return ctx_ref<time_state>(ctx).fixed_rate; }

rng &random(context &ctx) { return ctx_ref<rng>(ctx); }

entt::registry &world(context &ctx) { return ctx_ref<ecs_store>(ctx)->registry; }
entt::dispatcher &events(context &ctx) { return ctx_ref<ecs_store>(ctx)->dispatcher; }
bool key_pressed(const context &ctx, key_code key) {
  return input_key_pressed(ctx_ref<input_store>(ctx), key);
}

bool key_held(const context &ctx, key_code key) {
  return input_key_held(ctx_ref<input_store>(ctx), key);
}

bool key_released(const context &ctx, key_code key) {
  return input_key_released(ctx_ref<input_store>(ctx), key);
}

action_handle action_register(context &ctx, const char *name) {
  return input_action_register(ctx_ref<input_store>(ctx), name);
}

action_handle action_find(const context &ctx, const char *name) {
  return input_action_find(ctx_ref<input_store>(ctx), name);
}

void action_bind_key(context &ctx, action_handle handle, key_code key) {
  input_action_bind_key(ctx_ref<input_store>(ctx), handle, key);
}

bool action_pressed(const context &ctx, action_handle handle) {
  return input_action_pressed(ctx_ref<input_store>(ctx), handle);
}

bool action_held(const context &ctx, action_handle handle) {
  return input_action_held(ctx_ref<input_store>(ctx), handle);
}

bool action_released(const context &ctx, action_handle handle) {
  return input_action_released(ctx_ref<input_store>(ctx), handle);
}

void action_bind_mouse(context &ctx, action_handle handle,
                       mouse_button button) {
  input_action_bind_mouse(ctx_ref<input_store>(ctx), handle, button);
}

void action_bind_pad(context &ctx, action_handle handle,
                     gamepad_button button) {
  input_action_bind_pad(ctx_ref<input_store>(ctx), handle, button);
}

void action_clear_binds(context &ctx, action_handle handle) {
  input_action_clear_binds(ctx_ref<input_store>(ctx), handle);
}

// In the UI canvas (ui_canvas_begin()), in its units.
vec2 mouse_pos(const context &ctx) {
  const vec2 p = input_mouse_pos(ctx_ref<input_store>(ctx));
  return view_in_canvas(ctx_ref<view_state>(ctx)) ? p * (1.0f / ctx_ref<view_state>(ctx)->canvas_scale) : p;
}

vec2 mouse_delta(const context &ctx) {
  const vec2 d = input_mouse_delta(ctx_ref<input_store>(ctx));
  return view_in_canvas(ctx_ref<view_state>(ctx)) ? d * (1.0f / ctx_ref<view_state>(ctx)->canvas_scale) : d;
}

f32 mouse_wheel(const context &ctx) { return input_mouse_wheel(ctx_ref<input_store>(ctx)); }

bool mouse_pressed(const context &ctx, mouse_button button) {
  return input_mouse_pressed(ctx_ref<input_store>(ctx), button);
}

bool mouse_held(const context &ctx, mouse_button button) {
  return input_mouse_held(ctx_ref<input_store>(ctx), button);
}

bool mouse_released(const context &ctx, mouse_button button) {
  return input_mouse_released(ctx_ref<input_store>(ctx), button);
}

bool pad_available(const context &ctx, i32 pad) {
  return input_pad_available(ctx_ref<input_store>(ctx), pad);
}

bool pad_pressed(const context &ctx, i32 pad, gamepad_button button) {
  return input_pad_pressed(ctx_ref<input_store>(ctx), pad, button);
}

bool pad_held(const context &ctx, i32 pad, gamepad_button button) {
  return input_pad_held(ctx_ref<input_store>(ctx), pad, button);
}

bool pad_released(const context &ctx, i32 pad, gamepad_button button) {
  return input_pad_released(ctx_ref<input_store>(ctx), pad, button);
}

f32 pad_axis(const context &ctx, i32 pad, gamepad_axis axis) {
  return input_pad_axis(ctx_ref<input_store>(ctx), pad, axis);
}

void pad_set_deadzone(context &ctx, f32 deadzone) {
  input_pad_set_deadzone(ctx_ref<input_store>(ctx), deadzone);
}

i32 text_count(const context &ctx) { return input_text_count(ctx_ref<input_store>(ctx)); }

i32 text_char(const context &ctx, i32 index) {
  return input_text_char(ctx_ref<input_store>(ctx), index);
}

void key_consume(context &ctx, key_code key) {
  input_key_consume(ctx_ref<input_store>(ctx), key);
}

void mouse_consume(context &ctx, mouse_button button) {
  input_mouse_consume(ctx_ref<input_store>(ctx), button);
}

void mouse_wheel_consume(context &ctx) {
  input_mouse_wheel_consume(ctx_ref<input_store>(ctx));
}

axis_handle axis_register(context &ctx, const char *name) {
  return input_axis_register(ctx_ref<input_store>(ctx), name);
}

axis_handle axis_find(const context &ctx, const char *name) {
  return input_axis_find(ctx_ref<input_store>(ctx), name);
}

void axis_bind_keys(context &ctx, axis_handle handle, key_code negative,
                    key_code positive) {
  input_axis_bind_keys(ctx_ref<input_store>(ctx), handle, negative, positive);
}

void axis_bind_pad(context &ctx, axis_handle handle, gamepad_axis axis) {
  input_axis_bind_pad(ctx_ref<input_store>(ctx), handle, axis);
}

void axis_clear_binds(context &ctx, axis_handle handle) {
  input_axis_clear_binds(ctx_ref<input_store>(ctx), handle);
}

f32 axis_value(const context &ctx, axis_handle handle) {
  return input_axis_value(ctx_ref<input_store>(ctx), handle);
}

shader_handle shader_load(context &ctx, const char *vspath,
                          const char *fspath) {
  return shader_store_load(ctx_ref<shader_store>(ctx), vspath, fspath);
}

void shader_unload(context &ctx, shader_handle handle) {
  shader_store_unload(ctx_ref<shader_store>(ctx), handle);
}

void shader_begin(const context &ctx, shader_handle handle) {
  shader_store_begin(ctx_ref<shader_store>(ctx), handle);
  const shader_slot *slot = shader_slot_of(ctx_ref<shader_store>(ctx), handle);
  if (slot != nullptr)
    shader_bind_textures(ctx, *slot);
}

void shader_end(const context &ctx) { shader_store_end(ctx_ref<shader_store>(ctx)); }

void shader_set_i32(context &ctx, shader_handle handle, const char *name,
                    i32 value) {
  shader_store_set_i32(ctx_ref<shader_store>(ctx), handle, name, value);
}

void shader_set_f32(context &ctx, shader_handle handle, const char *name,
                    f32 value) {
  shader_store_set_f32(ctx_ref<shader_store>(ctx), handle, name, value);
}

void shader_set_vec2(context &ctx, shader_handle handle, const char *name,
                     vec2 value) {
  shader_store_set_vec2(ctx_ref<shader_store>(ctx), handle, name, value);
}

void shader_set_vec3(context &ctx, shader_handle handle, const char *name,
                     vec3 value) {
  shader_store_set_vec3(ctx_ref<shader_store>(ctx), handle, name, value);
}

void shader_set_vec4(context &ctx, shader_handle handle, const char *name,
                     vec4 value) {
  shader_store_set_rgba(ctx_ref<shader_store>(ctx), handle, name, value);
}

void shader_set_vec4_array(context &ctx, shader_handle handle, const char *name,
                           const vec4 *values, u32 count) {
  shader_store_set_vec4_array(ctx_ref<shader_store>(ctx), handle, name, values, count);
}

void shader_set_texture(context &ctx, shader_handle handle, const char *name, texture_handle texture) {
  const texture_slot *slot = texture_slot_of(ctx_ref<texture_store>(ctx), texture);
  if (slot == nullptr) {
    NJIN_WARN("shader: '%s' not set, the texture is not valid", name != nullptr ? name : "");
    return;
  }
  if (slot->packed) {
    NJIN_WARN("shader: '%s' not set, the texture is packed in an atlas (load it with texture_load)",
              name != nullptr ? name : "");
    return;
  }
  shader_store_set_texture(ctx_ref<shader_store>(ctx), handle, name, false, texture.id);
}

void shader_set_texture(context &ctx, shader_handle handle, const char *name,
                        render_texture_handle texture) {
  if (render_texture_slot_of(ctx_ref<render_texture_store>(ctx), texture) == nullptr) {
    NJIN_WARN("shader: '%s' not set, the render texture is not valid", name != nullptr ? name : "");
    return;
  }
  shader_store_set_texture(ctx_ref<shader_store>(ctx), handle, name, true, texture.id);
}

texture_handle texture_load(context &ctx, const char *path) {
  return texture_store_load(ctx_ref<texture_store>(ctx), path);
}

texture_handle texture_from_pixels(context &ctx, const u8 *rgba, u32 width, u32 height) {
  return texture_store_from_pixels(ctx_ref<texture_store>(ctx), rgba, width, height);
}

void texture_unload(context &ctx, texture_handle handle) {
  texture_store_unload(ctx_ref<texture_store>(ctx), handle);
}

vec2 texture_size(const context &ctx, texture_handle handle) {
  return texture_store_size(ctx_ref<texture_store>(ctx), handle);
}

void texture_set_shader(context &ctx, texture_handle handle,
                        shader_handle shader) {
  texture_store_set_shader(ctx_ref<texture_store>(ctx), handle, shader);
}

void texture_draw(const context &ctx, texture_handle handle, vec2 pos,
                  rgba tint) {
  const texture_slot *slot = texture_slot_of(ctx_ref<texture_store>(ctx), handle);
  // A material shader auto-binds only when nothing else is already bound: an
  // explicit shader_begin() (the game's own choice) always wins.
  const bool material = slot != nullptr && slot->shader.id != 0 && ctx_ref<shader_store>(ctx)->active.id == 0;
  if (material)
    shader_begin(ctx, slot->shader);
  texture_store_draw(ctx_ref<texture_store>(ctx), handle, pos, tint);
  if (material)
    shader_end(ctx);
}

render_texture_handle render_texture_load(context &ctx, u32 width,
                                          u32 height) {
  return render_texture_store_load(ctx_ref<render_texture_store>(ctx), width, height);
}

void render_texture_unload(context &ctx, render_texture_handle handle) {
  render_texture_store_unload(ctx_ref<render_texture_store>(ctx), handle);
}

vec2 render_texture_size(const context &ctx, render_texture_handle handle) {
  return render_texture_store_size(ctx_ref<render_texture_store>(ctx), handle);
}

void render_texture_begin(const context &ctx, render_texture_handle handle) {
  view_ui_suspend(ctx_ref<view_state>(ctx));
  if (render_texture_store_begin(ctx_ref<render_texture_store>(ctx), handle))
    ctx_ref<view_state>(ctx)->offscreen_depth++;
  else if (ctx_ref<view_state>(ctx)->ui_window)
    view_rebind(ctx_ref<view_state>(ctx));
  else
    view_canvas_sync(ctx_ref<view_state>(ctx));
}

void render_texture_begin(const context &ctx, render_texture_handle handle,
                          rgba clear) {
  view_ui_suspend(ctx_ref<view_state>(ctx));
  if (render_texture_store_begin(ctx_ref<render_texture_store>(ctx), handle)) {
    ctx_ref<view_state>(ctx)->offscreen_depth++;
    render_texture_store_clear(clear);
  } else if (ctx_ref<view_state>(ctx)->ui_window) {
    view_rebind(ctx_ref<view_state>(ctx));
  } else {
    view_canvas_sync(ctx_ref<view_state>(ctx));
  }
}

void render_texture_end(const context &ctx) {
  render_texture_store_end();
  ctx_ref<view_state>(ctx)->offscreen_depth = std::max(0, ctx_ref<view_state>(ctx)->offscreen_depth - 1);
  view_rebind(ctx_ref<view_state>(ctx)); // back to the virtual screen, when drawing into one
}

bool render_texture_save(context &ctx, render_texture_handle handle, const char *path) {
  return render_texture_store_save(ctx_ref<render_texture_store>(ctx), handle, path);
}

void render_texture_set_filter(context &ctx, render_texture_handle handle,
                               texture_filter filter) {
  render_texture_store_set_filter(ctx_ref<render_texture_store>(ctx), handle, filter);
}

void render_texture_draw(const context &ctx, render_texture_handle handle,
                         vec2 pos, rgba tint) {
  render_texture_store_draw(ctx_ref<render_texture_store>(ctx), handle, pos, tint);
}

sound_handle sound_load(context &ctx, const char *path) {
  return sound_store_load(ctx_ref<audio_store>(ctx), path);
}

sound_handle sound_load_samples(context &ctx, const f32 *samples, i32 count,
                                i32 sample_rate) {
  return sound_store_load_samples(ctx_ref<audio_store>(ctx), samples, count, sample_rate);
}

void sound_unload(context &ctx, sound_handle handle) {
  sound_store_unload(ctx_ref<audio_store>(ctx), handle);
}

void sound_set_volume(context &ctx, sound_handle handle, f32 volume) {
  sound_store_set_volume(ctx_ref<audio_store>(ctx), handle, volume);
}

void sound_set_muted(context &ctx, sound_handle handle, bool muted) {
  sound_store_set_muted(ctx_ref<audio_store>(ctx), handle, muted);
}

void sound_play_once(context &ctx, sound_handle handle) {
  sound_store_play_once(ctx_ref<audio_store>(ctx), handle, 1.0f, 1.0f);
}

void sound_play_once_at(context &ctx, sound_handle handle, f32 pitch,
                        f32 gain) {
  sound_store_play_once(ctx_ref<audio_store>(ctx), handle, pitch, gain);
}

void sound_play_restart(context &ctx, sound_handle handle) {
  sound_store_play_restart(ctx_ref<audio_store>(ctx), handle);
}

void sound_play_loop(context &ctx, sound_handle handle) {
  sound_store_play_loop(ctx_ref<audio_store>(ctx), handle);
}

void audio_set_bus_volume(context &ctx, audio_bus bus, f32 volume) {
  audio_store_set_bus_volume(ctx_ref<audio_store>(ctx), bus, volume);
}

f32 audio_bus_volume(const context &ctx, audio_bus bus) {
  return bus >= bus_master && bus < audio_bus_count ? ctx_ref<audio_store>(ctx)->bus_volume[bus] : 0.0f;
}

void audio_set_bus_muted(context &ctx, audio_bus bus, bool muted) {
  audio_store_set_bus_muted(ctx_ref<audio_store>(ctx), bus, muted);
}

bool audio_bus_muted(const context &ctx, audio_bus bus) {
  return bus >= bus_master && bus < audio_bus_count && ctx_ref<audio_store>(ctx)->bus_muted[bus];
}

void sound_set_bus(context &ctx, sound_handle handle, audio_bus bus) {
  sound_store_set_bus(ctx_ref<audio_store>(ctx), handle, bus);
}

bool music_playing(context &ctx, music_handle handle) {
  return music_store_playing(ctx_ref<audio_store>(ctx), handle);
}

void music_fade_in(context &ctx, music_handle handle, f32 seconds) {
  music_store_fade(ctx_ref<audio_store>(ctx), handle, 1.0f, seconds, true, false);
}

void music_fade_out(context &ctx, music_handle handle, f32 seconds) {
  music_store_fade(ctx_ref<audio_store>(ctx), handle, 0.0f, seconds, false, true);
}

void music_crossfade(context &ctx, music_handle handle, f32 seconds) {
  for (usize i = 0; i < ctx_ref<audio_store>(ctx)->musics.size(); i++) {
    const music_handle other{(u32)(i + 1)};
    if (other.id != handle.id && music_store_playing(ctx_ref<audio_store>(ctx), other))
      music_store_fade(ctx_ref<audio_store>(ctx), other, 0.0f, seconds, false, true);
  }
  if (handle.id != 0)
    music_store_fade(ctx_ref<audio_store>(ctx), handle, 1.0f, seconds, true, false);
}

void sound_play_at(context &ctx, sound_handle handle, vec2 world_pos) {
  const audio_store &audio = ctx_ref<audio_store>(ctx);
  // The listener is where the camera looks, so what is centred on screen is
  // heard loudest.
  const f32 dist = distance(camera_active(ctx).target, world_pos);
  f32 gain = 1.0f;
  if (dist >= audio.range_far)
    gain = 0.0f;
  else if (dist > audio.range_near)
    gain = 1.0f - (dist - audio.range_near) / (audio.range_far - audio.range_near);
  if (gain <= 0.0f)
    return;
  // Pan from where it lands on screen: the left edge is hard left.
  const f32 half_w = (f32)screen_size(ctx).x * 0.5f;
  const f32 pan = half_w > 0.0f ? (w2scr(ctx, world_pos).x - half_w) / half_w : 0.0f;
  sound_store_play_once(ctx_ref<audio_store>(ctx), handle, 1.0f, gain, clamp(pan, -1.0f, 1.0f));
}

void audio_set_range(context &ctx, f32 full_until, f32 silent_from) {
  const f32 lo = full_until < 0.0f ? 0.0f : full_until;
  ctx_ref<audio_store>(ctx)->range_near = lo;
  // Kept strictly above the near range so the fade never divides by zero.
  ctx_ref<audio_store>(ctx)->range_far = silent_from > lo ? silent_from : lo + 1e-3f;
}

void sound_stop(context &ctx, sound_handle handle) {
  sound_store_stop(ctx_ref<audio_store>(ctx), handle);
}

music_handle music_load(context &ctx, const char *path) {
  return music_store_load(ctx_ref<audio_store>(ctx), path);
}

void music_unload(context &ctx, music_handle handle) {
  music_store_unload(ctx_ref<audio_store>(ctx), handle);
}

void music_set_volume(context &ctx, music_handle handle, f32 volume) {
  music_store_set_volume(ctx_ref<audio_store>(ctx), handle, volume);
}

void music_set_muted(context &ctx, music_handle handle, bool muted) {
  music_store_set_muted(ctx_ref<audio_store>(ctx), handle, muted);
}

void music_set_looping(context &ctx, music_handle handle, bool looping) {
  music_store_set_looping(ctx_ref<audio_store>(ctx), handle, looping);
}

void music_play(context &ctx, music_handle handle) {
  music_store_play(ctx_ref<audio_store>(ctx), handle);
}

void music_stop(context &ctx, music_handle handle) {
  music_store_stop(ctx_ref<audio_store>(ctx), handle);
}

void music_pause(context &ctx, music_handle handle) {
  music_store_pause(ctx_ref<audio_store>(ctx), handle);
}

void music_resume(context &ctx, music_handle handle) {
  music_store_resume(ctx_ref<audio_store>(ctx), handle);
}
} // namespace njin
