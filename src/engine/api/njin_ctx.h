#pragma once
#include "_types.h"

namespace njin {
// Opaque: created with njin_create (njin.h), only accessed through the
// functions below.
struct njin_ctx;

// Time
void get_delta(const njin_ctx &ctx, f32 &delta);
void get_elapsed(const njin_ctx &ctx, f32 &time);

// Coordinates
void world_to_screen(const njin_ctx &ctx, vec2 world, vec2 &screen);
void screen_to_world(const njin_ctx &ctx, vec2 screen, vec2 &world);

// Keys
bool key_pressed(const njin_ctx &ctx, key_code key);
bool key_held(const njin_ctx &ctx, key_code key);
bool key_released(const njin_ctx &ctx, key_code key);

// Actions
action_handle action_register(njin_ctx &ctx, const char *name);
action_handle action_find(const njin_ctx &ctx, const char *name);
void action_bind_key(njin_ctx &ctx, action_handle handle, key_code key);
bool action_pressed(const njin_ctx &ctx, action_handle handle);
bool action_held(const njin_ctx &ctx, action_handle handle);
bool action_released(const njin_ctx &ctx, action_handle handle);

// Shaders
// Either path may be nullptr to keep the default stage. Returns a handle with
// id 0 if a file is missing or the shader fails to compile. Handles that are
// invalid or already unloaded are ignored by every call below.
shader_handle shader_load(njin_ctx &ctx, const char *vspath,
                          const char *fspath);
void shader_unload(njin_ctx &ctx, shader_handle handle);

// Must be called between frame begin/end. Set uniforms before shader_begin.
void shader_begin(const njin_ctx &ctx, shader_handle handle);
void shader_end(const njin_ctx &ctx);

// A uniform that does not exist is logged once and then skipped.
void shader_set_i32(njin_ctx &ctx, shader_handle handle, const char *name,
                    i32 value);
void shader_set_f32(njin_ctx &ctx, shader_handle handle, const char *name,
                    f32 value);
void shader_set_vec2(njin_ctx &ctx, shader_handle handle,
                     const char *name, vec2 value);
void shader_set_vec4(njin_ctx &ctx, shader_handle handle, const char *name, vec4 value);
} // namespace njin
