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

// Coordinates
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
} // namespace njin
