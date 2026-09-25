#pragma once
#include "_mod.h"
#include "_types.h"
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

namespace njin {
// Opaque: created with njin_create (njin.h), only accessed through the
// functions below.
struct njin_ctx;

// ECS access. EnTT is part of the module API so systems can define their own
// components and query the registry directly.
entt::registry &world(njin_ctx &ctx);
entt::dispatcher &events(njin_ctx &ctx);
// Adds a system to the schedule. Only valid inside a module's setup callback.
// Within one module and phase, systems are ordered by sys_desc (see _mod.h);
// modules run in the order they were registered.
void ecs_register(njin_ctx &ctx, sys_phase phase, sys_fnc fnc);
void ecs_register(njin_ctx &ctx, sys_phase phase, const sys_desc &desc);
// Runs the module's setup and schedules its systems. Must be called before
// njin_run. A module name can only be registered once.
void njin_mod_register(njin_ctx &ctx, const mod_desc &desc);

// Time
f32 delta(const njin_ctx &ctx);
f32 elapsed(const njin_ctx &ctx);

// Camera
// The view used this frame: the entity with camera_on + camera_2d + transform
// (see _comps.h), or the identity view (world == screen pixels) if none.
// Read live from the registry, so changes apply immediately.
camera_view camera_active(const njin_ctx &ctx);
// World <-> screen pixels through camera_active.
vec2 w2scr(const njin_ctx &ctx, vec2 pos);
vec2 scr2w(const njin_ctx &ctx, vec2 pos);

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
void shader_set_vec2(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec2 value);
void shader_set_vec4(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec4 value);

// Textures
// Returns a handle with id 0 if the file is missing or cannot be decoded.
// Invalid or already unloaded handles are ignored by every call below.
texture_handle texture_load(njin_ctx &ctx, const char *path);
void texture_unload(njin_ctx &ctx, texture_handle handle);
// Size in pixels, {0, 0} for an invalid handle.
vec2 texture_size(const njin_ctx &ctx, texture_handle handle);
// Draws with the top-left corner at pos. tint multiplies the pixels; white
// ({1, 1, 1, 1}) draws the texture unchanged.
void texture_draw(const njin_ctx &ctx, texture_handle handle, vec2 pos,
                  rgba tint);

// Render textures
// An offscreen image you can draw into, then draw like a texture.
// Returns a handle with id 0 if the size is 0 or creation fails.
render_texture_handle render_texture_load(njin_ctx &ctx, u32 width,
                                          u32 height);
void render_texture_unload(njin_ctx &ctx, render_texture_handle handle);
vec2 render_texture_size(const njin_ctx &ctx, render_texture_handle handle);
// Draw calls between begin and end go into the render texture, in its own
// pixel space (no camera). The overload with a color clears it first;
// without one, the previous contents are kept.
// Begin resets the camera transform, so do not use it in pre_render/render:
// draw into render textures in post_update or post_render instead.
void render_texture_begin(const njin_ctx &ctx, render_texture_handle handle);
void render_texture_begin(const njin_ctx &ctx, render_texture_handle handle,
                          rgba clear);
void render_texture_end(const njin_ctx &ctx);
// Draws the render texture's contents upright, top-left corner at pos.
void render_texture_draw(const njin_ctx &ctx, render_texture_handle handle,
                         vec2 pos, rgba tint);
} // namespace njin
