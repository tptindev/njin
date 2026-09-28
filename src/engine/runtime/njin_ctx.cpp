#include "njin_ctx.h"
#include "njin_audio.h"
#include "njin_camera.h"
#include "njin_input.h"
#include "njin_render.h"
#include "njin_ctx_impl.h"
#include "njin_cfg.h"
#include "njin_log.h"
#include <algorithm>
#include <entt/entity/registry.hpp>

namespace njin {
f32 delta(const njin_ctx &ctx) {
  return ctx.time.in_fixed ? ctx.time.fixed_dt : ctx.time.dt;
}
f32 delta_real(const njin_ctx &ctx) { return ctx.time.dt_real; }
f32 elapsed(const njin_ctx &ctx) { return ctx.time.elapsed; }

void time_set_scale(njin_ctx &ctx, f32 scale) {
  ctx.time.scale = scale < 0.0f ? 0.0f : scale;
}
f32 time_scale(const njin_ctx &ctx) { return ctx.time.scale; }
void time_set_paused(njin_ctx &ctx, bool paused) { ctx.time.paused = paused; }
bool time_paused(const njin_ctx &ctx) { return ctx.time.paused; }
f32 fixed_delta(const njin_ctx &ctx) { return ctx.time.fixed_dt; }
f32 fixed_alpha(const njin_ctx &ctx) { return ctx.time.fixed_alpha; }

rng &random(njin_ctx &ctx) { return ctx.random; }

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
  const shader_slot *slot = shader_slot_of(ctx.shader, handle);
  if (slot != nullptr)
    shader_bind_textures(ctx, *slot);
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

void shader_set_vec3(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec3 value) {
  shader_store_set_vec3(ctx.shader, handle, name, value);
}

void shader_set_vec4(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec4 value) {
  shader_store_set_rgba(ctx.shader, handle, name, value);
}

void shader_set_vec4_array(njin_ctx &ctx, shader_handle handle, const char *name,
                           const vec4 *values, u32 count) {
  shader_store_set_vec4_array(ctx.shader, handle, name, values, count);
}

void shader_set_texture(njin_ctx &ctx, shader_handle handle, const char *name, texture_handle texture) {
  const texture_slot *slot = texture_slot_of(ctx.texture, texture);
  if (slot == nullptr) {
    NJIN_WARN("shader: '%s' not set, the texture is not valid", name != nullptr ? name : "");
    return;
  }
  if (slot->packed) {
    NJIN_WARN("shader: '%s' not set, the texture is packed in an atlas (load it with texture_load)",
              name != nullptr ? name : "");
    return;
  }
  shader_store_set_texture(ctx.shader, handle, name, false, texture.id);
}

void shader_set_texture(njin_ctx &ctx, shader_handle handle, const char *name,
                        render_texture_handle texture) {
  if (render_texture_slot_of(ctx.render_texture, texture) == nullptr) {
    NJIN_WARN("shader: '%s' not set, the render texture is not valid", name != nullptr ? name : "");
    return;
  }
  shader_store_set_texture(ctx.shader, handle, name, true, texture.id);
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
  view_ui_suspend(ctx.view);
  if (render_texture_store_begin(ctx.render_texture, handle))
    ctx.view.offscreen_depth++;
  else if (ctx.view.ui_window)
    view_rebind(ctx.view);
}

void render_texture_begin(const njin_ctx &ctx, render_texture_handle handle,
                          rgba clear) {
  view_ui_suspend(ctx.view);
  if (render_texture_store_begin(ctx.render_texture, handle)) {
    ctx.view.offscreen_depth++;
    render_texture_store_clear(clear);
  } else if (ctx.view.ui_window) {
    view_rebind(ctx.view);
  }
}

void render_texture_end(const njin_ctx &ctx) {
  render_texture_store_end();
  ctx.view.offscreen_depth = std::max(0, ctx.view.offscreen_depth - 1);
  view_rebind(ctx.view); // back to the virtual screen, when drawing into one
}

bool render_texture_save(njin_ctx &ctx, render_texture_handle handle, const char *path) {
  return render_texture_store_save(ctx.render_texture, handle, path);
}

void render_texture_set_filter(njin_ctx &ctx, render_texture_handle handle,
                               texture_filter filter) {
  render_texture_store_set_filter(ctx.render_texture, handle, filter);
}

void render_texture_draw(const njin_ctx &ctx, render_texture_handle handle,
                         vec2 pos, rgba tint) {
  render_texture_store_draw(ctx.render_texture, handle, pos, tint);
}

sound_handle sound_load(njin_ctx &ctx, const char *path) {
  return sound_store_load(ctx.audio, path);
}

sound_handle sound_load_samples(njin_ctx &ctx, const f32 *samples, i32 count,
                                i32 sample_rate) {
  return sound_store_load_samples(ctx.audio, samples, count, sample_rate);
}

void sound_unload(njin_ctx &ctx, sound_handle handle) {
  sound_store_unload(ctx.audio, handle);
}

void sound_set_volume(njin_ctx &ctx, sound_handle handle, f32 volume) {
  sound_store_set_volume(ctx.audio, handle, volume);
}

void sound_set_muted(njin_ctx &ctx, sound_handle handle, bool muted) {
  sound_store_set_muted(ctx.audio, handle, muted);
}

void sound_play_once(njin_ctx &ctx, sound_handle handle) {
  sound_store_play_once(ctx.audio, handle, 1.0f, 1.0f);
}

void sound_play_once_at(njin_ctx &ctx, sound_handle handle, f32 pitch,
                        f32 gain) {
  sound_store_play_once(ctx.audio, handle, pitch, gain);
}

void sound_play_restart(njin_ctx &ctx, sound_handle handle) {
  sound_store_play_restart(ctx.audio, handle);
}

void sound_play_loop(njin_ctx &ctx, sound_handle handle) {
  sound_store_play_loop(ctx.audio, handle);
}

void audio_set_bus_volume(njin_ctx &ctx, audio_bus bus, f32 volume) {
  audio_store_set_bus_volume(ctx.audio, bus, volume);
}

f32 audio_bus_volume(const njin_ctx &ctx, audio_bus bus) {
  return bus >= bus_master && bus < audio_bus_count ? ctx.audio.bus_volume[bus] : 0.0f;
}

void audio_set_bus_muted(njin_ctx &ctx, audio_bus bus, bool muted) {
  audio_store_set_bus_muted(ctx.audio, bus, muted);
}

bool audio_bus_muted(const njin_ctx &ctx, audio_bus bus) {
  return bus >= bus_master && bus < audio_bus_count && ctx.audio.bus_muted[bus];
}

void sound_set_bus(njin_ctx &ctx, sound_handle handle, audio_bus bus) {
  sound_store_set_bus(ctx.audio, handle, bus);
}

bool music_playing(njin_ctx &ctx, music_handle handle) {
  return music_store_playing(ctx.audio, handle);
}

void music_fade_in(njin_ctx &ctx, music_handle handle, f32 seconds) {
  music_store_fade(ctx.audio, handle, 1.0f, seconds, true, false);
}

void music_fade_out(njin_ctx &ctx, music_handle handle, f32 seconds) {
  music_store_fade(ctx.audio, handle, 0.0f, seconds, false, true);
}

void music_crossfade(njin_ctx &ctx, music_handle handle, f32 seconds) {
  for (usize i = 0; i < ctx.audio.musics.size(); i++) {
    const music_handle other{(u32)(i + 1)};
    if (other.id != handle.id && music_store_playing(ctx.audio, other))
      music_store_fade(ctx.audio, other, 0.0f, seconds, false, true);
  }
  if (handle.id != 0)
    music_store_fade(ctx.audio, handle, 1.0f, seconds, true, false);
}

void sound_play_at(njin_ctx &ctx, sound_handle handle, vec2 world_pos) {
  const audio_store &audio = ctx.audio;
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
  sound_store_play_once(ctx.audio, handle, 1.0f, gain, clamp(pan, -1.0f, 1.0f));
}

void audio_set_range(njin_ctx &ctx, f32 full_until, f32 silent_from) {
  const f32 lo = full_until < 0.0f ? 0.0f : full_until;
  ctx.audio.range_near = lo;
  // Kept strictly above the near range so the fade never divides by zero.
  ctx.audio.range_far = silent_from > lo ? silent_from : lo + 1e-3f;
}

void sound_stop(njin_ctx &ctx, sound_handle handle) {
  sound_store_stop(ctx.audio, handle);
}

music_handle music_load(njin_ctx &ctx, const char *path) {
  return music_store_load(ctx.audio, path);
}

void music_unload(njin_ctx &ctx, music_handle handle) {
  music_store_unload(ctx.audio, handle);
}

void music_set_volume(njin_ctx &ctx, music_handle handle, f32 volume) {
  music_store_set_volume(ctx.audio, handle, volume);
}

void music_set_muted(njin_ctx &ctx, music_handle handle, bool muted) {
  music_store_set_muted(ctx.audio, handle, muted);
}

void music_set_looping(njin_ctx &ctx, music_handle handle, bool looping) {
  music_store_set_looping(ctx.audio, handle, looping);
}

void music_play(njin_ctx &ctx, music_handle handle) {
  music_store_play(ctx.audio, handle);
}

void music_stop(njin_ctx &ctx, music_handle handle) {
  music_store_stop(ctx.audio, handle);
}

void music_pause(njin_ctx &ctx, music_handle handle) {
  music_store_pause(ctx.audio, handle);
}

void music_resume(njin_ctx &ctx, music_handle handle) {
  music_store_resume(ctx.audio, handle);
}
} // namespace njin
