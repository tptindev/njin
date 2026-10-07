#include "_math.h"
#include "njin_3d.h"
#include "njin_audio_impl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_physics3d.h"
#include <algorithm>
#include <cmath>
#include <entt/entity/registry.hpp>
#include <vector>

namespace njin {
namespace {
bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Said once per function, as physics3d does, so a bad value shows where it came from.
bool refuse(bool ok, const char *what) {
  if (ok)
    return false;
  static std::vector<const char *> told;
  if (std::find(told.begin(), told.end(), what) == told.end()) {
    told.push_back(what);
    NJIN_WARN("audio: %s given a value that is not finite (NaN or infinite): ignored", what);
  }
  return true;
}

// Velocities measured from frame-to-frame motion are eased over this many
// seconds: a source moved at the fixed step travels 0 or 2 steps in a frame,
// and the raw quotient would make the Doppler pitch warble.
constexpr f32 velocity_smoothing = 0.1f;
// Occlusion eases over this many seconds (about 0.15 s to settle).
constexpr f32 occlusion_smoothing = 0.05f;

vec3 ease_velocity(vec3 current, vec3 measured, f32 dt) {
  const f32 k = 1.0f - std::exp(-dt / velocity_smoothing);
  return current + (measured - current) * k;
}

bool entity_position(context &ctx, entt::entity e, vec3 &out) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(e))
    return false;
  const transform3d *t = reg.try_get<transform3d>(e);
  if (t == nullptr)
    return false;
  out = t->position;
  return true;
}

voice3d_handle start(context &ctx, sound_handle sound, vec3 position, const sound3d_desc &desc, bool looping,
                     const char *what) {
  if (refuse(finite3(position), what))
    return voice3d_handle{};
  return voice3d_store_start(ctx.audio, sound, position, desc, looping);
}

voice3d_handle start_on(context &ctx, sound_handle sound, entt::entity e, const sound3d_desc &desc, bool looping) {
  vec3 position{};
  if (!entity_position(ctx, e, position)) {
    NJIN_WARN("audio: a 3D sound on an entity that is gone or has no transform3d is not played");
    return voice3d_handle{};
  }
  const voice3d_handle h = voice3d_store_start(ctx.audio, sound, position, desc, looping);
  if (voice3d_slot *v = voice3d_slot_of(ctx.audio, h)) {
    v->attached = true;
    v->entity = e;
  }
  return h;
}

void update_listener(audio_store &a, f32 dt) {
  if (!a.listener_follow || !a.camera_seen)
    return;
  const vec3 velocity = a.listener.velocity;
  a.listener = a.camera;
  a.listener.velocity = velocity;
  if (a.listener_has_last && dt > 0.0f)
    a.listener.velocity = ease_velocity(velocity, (a.listener.position - a.listener_last) / dt, dt);
  a.listener_last = a.listener.position;
  a.listener_has_last = true;
}

void update_occlusion(const context &ctx, const audio_store &a, voice3d_slot &v, f32 dt) {
  f32 target = 1.0f;
  v.mix.occluded = false;
  if (v.desc.occlusion) {
    const vec3 to_source = v.position - a.listener.position;
    const f32 dist = length(to_source);
    const f32 margin = std::max(v.desc.occlusion_margin, 0.0f);
    if (dist > margin && dist < std::max(v.desc.max_distance, 0.0f)) {
      const ray3d ray{.origin = a.listener.position, .direction = to_source / dist};
      const ray3d_hit hit = physics3d_raycast(ctx, ray, dist - margin);
      if (hit.hit) {
        v.mix.occluded = true;
        target = clamp(v.desc.occlusion_volume, 0.0f, 1.0f);
      }
    }
  }
  if (dt > 0.0f)
    v.occlusion += (target - v.occlusion) * (1.0f - std::exp(-dt / occlusion_smoothing));
  else
    v.occlusion = target;
}
} // namespace

void audio3d_update(context &ctx) {
  audio_store &a = ctx.audio;
  const f32 dt = ctx.time.dt_real;
  update_listener(a, dt);
  for (voice3d_slot &v : a.voices3d) {
    if (!v.alive)
      continue;
    if (!v.looping && !IsSoundPlaying(v.alias)) {
      voice3d_store_release(v);
      continue;
    }
    if (v.attached) {
      vec3 p{};
      if (entity_position(ctx, v.entity, p) && finite3(p)) {
        v.position = p + v.offset;
      } else if (v.looping) {
        voice3d_store_release(v); // its entity is gone
        continue;
      } else {
        v.attached = false; // a one-shot plays out where it was
      }
    }
    if (!v.velocity_set && dt > 0.0f) {
      if (v.has_last)
        v.velocity = ease_velocity(v.velocity, (v.position - v.last_position) / dt, dt);
      v.last_position = v.position;
      v.has_last = true;
    }
    update_occlusion(ctx, a, v, dt);
    voice3d_store_apply(a, v); // keeps the occluded flag just set
    if (v.looping && !IsSoundPlaying(v.alias))
      PlaySound(v.alias);
  }
}

void audio_set_listener3d(context &ctx, const audio_listener3d &listener) {
  if (refuse(finite3(listener.position) && finite3(listener.forward) && finite3(listener.up) &&
                 finite3(listener.velocity),
             "audio_set_listener3d"))
    return;
  ctx.audio.listener = listener;
  ctx.audio.listener_follow = false;
}

void audio_listener3d_follow_camera(context &ctx, bool follow) {
  ctx.audio.listener_follow = follow;
  ctx.audio.listener_has_last = false; // do not read the jump to the camera as speed
  if (!follow)
    ctx.audio.listener.velocity = {};
}

audio_listener3d audio_listener3d_get(const context &ctx) { return ctx.audio.listener; }

void audio_set_speed_of_sound(context &ctx, f32 units_per_second) {
  if (units_per_second > 0.0f && std::isfinite(units_per_second))
    ctx.audio.speed_of_sound = units_per_second;
}

voice3d_handle sound_play3d(context &ctx, sound_handle handle, vec3 position, const sound3d_desc &desc) {
  return start(ctx, handle, position, desc, false, "sound_play3d");
}

voice3d_handle sound_play3d(context &ctx, sound_handle handle, entt::entity entity, const sound3d_desc &desc) {
  return start_on(ctx, handle, entity, desc, false);
}

voice3d_handle sound_loop3d(context &ctx, sound_handle handle, vec3 position, const sound3d_desc &desc) {
  return start(ctx, handle, position, desc, true, "sound_loop3d");
}

voice3d_handle sound_loop3d(context &ctx, sound_handle handle, entt::entity entity, const sound3d_desc &desc) {
  return start_on(ctx, handle, entity, desc, true);
}

void voice3d_set_position(context &ctx, voice3d_handle voice, vec3 position) {
  voice3d_slot *v = voice3d_slot_of(ctx.audio, voice);
  if (v == nullptr || refuse(finite3(position), "voice3d_set_position"))
    return;
  v->attached = false;
  v->position = position;
}

void voice3d_attach(context &ctx, voice3d_handle voice, entt::entity entity, vec3 offset) {
  voice3d_slot *v = voice3d_slot_of(ctx.audio, voice);
  if (v == nullptr || refuse(finite3(offset), "voice3d_attach"))
    return;
  vec3 p{};
  if (!entity_position(ctx, entity, p)) {
    NJIN_WARN("audio: voice3d_attach to an entity that is gone or has no transform3d, ignored");
    return;
  }
  v->attached = true;
  v->entity = entity;
  v->offset = offset;
  v->position = p + offset;
}

void voice3d_set_velocity(context &ctx, voice3d_handle voice, vec3 velocity) {
  voice3d_slot *v = voice3d_slot_of(ctx.audio, voice);
  if (v == nullptr || refuse(finite3(velocity), "voice3d_set_velocity"))
    return;
  v->velocity = velocity;
  v->velocity_set = true;
}

void voice3d_set_desc(context &ctx, voice3d_handle voice, const sound3d_desc &desc) {
  voice3d_slot *v = voice3d_slot_of(ctx.audio, voice);
  if (v == nullptr || refuse(finite3(desc.cone_direction), "voice3d_set_desc"))
    return;
  v->desc = desc;
  voice3d_store_apply(ctx.audio, *v);
}

sound3d_desc voice3d_desc(const context &ctx, voice3d_handle voice) {
  const voice3d_slot *v = voice3d_slot_of(ctx.audio, voice);
  return v != nullptr ? v->desc : sound3d_desc{};
}

void voice3d_stop(context &ctx, voice3d_handle voice) {
  if (voice3d_slot *v = voice3d_slot_of(ctx.audio, voice))
    voice3d_store_release(*v);
}

bool voice3d_playing(const context &ctx, voice3d_handle voice) {
  const voice3d_slot *v = voice3d_slot_of(ctx.audio, voice);
  return v != nullptr && (v->looping || IsSoundPlaying(v->alias));
}

voice3d_mix voice3d_state(const context &ctx, voice3d_handle voice) {
  const voice3d_slot *v = voice3d_slot_of(ctx.audio, voice);
  return v != nullptr ? v->mix : voice3d_mix{};
}
} // namespace njin
