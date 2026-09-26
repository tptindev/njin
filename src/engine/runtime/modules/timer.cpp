#include "timer.h"
#include "_comps.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_scene.h"
#include <algorithm>

namespace njin {
namespace {
vec4 pack(vec2 v) { return {v.x, v.y, 0.0f, 0.0f}; }
vec4 pack(f32 v) { return {v, 0.0f, 0.0f, 0.0f}; }
vec4 pack(rgba c) { return {c.r, c.g, c.b, c.a}; }

bool owner_gone(const entt::registry &reg, bool owned, entt::entity owner) {
  return owned && !reg.valid(owner);
}

// Scene the job belongs to: dropped once the game has moved to another.
bool left_scene(const njin_ctx &ctx, u32 scene, bool keep) {
  return !keep && scene != scene_current(ctx).id;
}

// Writes the tween's value at eased progress `k` onto its target.
void apply(njin_ctx &ctx, tween_job &t, f32 k) {
  entt::registry &reg = world(ctx);
  const vec4 a = t.from;
  const vec4 b = t.to;
  const vec4 v{lerp(a.x, b.x, k), lerp(a.y, b.y, k), lerp(a.z, b.z, k), lerp(a.w, b.w, k)};
  switch (t.prop) {
  case tween_prop::pos:
    if (transform *tr = reg.try_get<transform>(t.owner))
      tr->pos = {v.x, v.y};
    break;
  case tween_prop::scale:
    if (transform *tr = reg.try_get<transform>(t.owner))
      tr->scale = v.x;
    break;
  case tween_prop::rot:
    if (transform *tr = reg.try_get<transform>(t.owner))
      tr->rot = v.x;
    break;
  case tween_prop::tint:
    if (sprite *s = reg.try_get<sprite>(t.owner))
      s->tint = {v.x, v.y, v.z, v.w};
    break;
  case tween_prop::custom:
    if (t.apply)
      t.apply(ctx, v.x);
    break;
  }
}

// The current value of what the tween drives, to start from.
bool capture(njin_ctx &ctx, tween_job &t) {
  entt::registry &reg = world(ctx);
  switch (t.prop) {
  case tween_prop::pos:
  case tween_prop::scale:
  case tween_prop::rot: {
    const transform *tr = reg.try_get<transform>(t.owner);
    if (tr == nullptr)
      return false;
    t.from = t.prop == tween_prop::pos ? pack(tr->pos) : pack(t.prop == tween_prop::scale ? tr->scale : tr->rot);
    return true;
  }
  case tween_prop::tint: {
    const sprite *s = reg.try_get<sprite>(t.owner);
    if (s == nullptr)
      return false;
    t.from = pack(s->tint);
    return true;
  }
  case tween_prop::custom:
    return true;
  }
  return false;
}

void run(njin_ctx &ctx) {
  timer_state &st = ctx.timers;
  const entt::registry &reg = world(ctx);
  // Timers. New ones made by a callback are appended; they start next frame.
  const usize timer_count = st.timers.size();
  for (usize i = 0; i < timer_count; i++) {
    timer_job &t = st.timers[i];
    if (t.dead)
      continue;
    if (owner_gone(reg, t.owned, t.desc.owner) ||
        left_scene(ctx, t.scene, t.desc.keep_across_scenes)) {
      t.dead = true;
      continue;
    }
    t.left -= t.desc.real_time ? ctx.time.dt_real : ctx.time.dt;
    if (t.left > 0.0f)
      continue;
    if (t.remaining > 0)
      t.remaining--;
    if (t.remaining == 0)
      t.dead = true;
    else
      t.left += std::max(t.interval, 1e-4f);
    // Copied out: the callback may add timers, which moves the vector.
    const std::function<void(njin_ctx &)> fn = t.fn;
    fn(ctx);
  }
  std::erase_if(st.timers, [](const timer_job &t) { return t.dead; });

  const usize tween_count = st.tweens.size();
  for (usize i = 0; i < tween_count; i++) {
    tween_job *t = &st.tweens[i];
    if (t->dead)
      continue;
    if (owner_gone(reg, t->owned, t->owner) || left_scene(ctx, t->scene, false)) {
      t->dead = true;
      continue;
    }
    f32 dt = t->desc.real_time ? ctx.time.dt_real : ctx.time.dt;
    if (t->wait > 0.0f) {
      t->wait -= dt;
      if (t->wait > 0.0f)
        continue;
      dt = -t->wait;
    }
    if (!t->started) {
      if (!capture(ctx, *t)) {
        t->dead = true;
        continue;
      }
      t->started = true;
    }
    t->time += dt;
    const f32 p = t->duration > 0.0f ? clamp(t->time / t->duration, 0.0f, 1.0f) : 1.0f;
    const f32 k = ease_apply(t->curve, t->forward ? p : 1.0f - p);
    apply(ctx, *t, k);
    t = &st.tweens[i]; // `apply` may have started tweens and moved the vector
    if (p < 1.0f)
      continue;
    if (t->runs_left != 0) {
      if (t->runs_left > 0)
        t->runs_left--;
      t->time = 0.0f;
      if (t->desc.yoyo)
        t->forward = !t->forward;
      continue;
    }
    t->dead = true;
    if (t->desc.done) {
      const std::function<void(njin_ctx &)> done = t->desc.done;
      done(ctx);
    }
  }
  std::erase_if(st.tweens, [](const tween_job &t) { return t.dead; });
}

void setup(njin_ctx &ctx) { ecs_register(ctx, phase_update, run, "run"); }

tween_handle start(njin_ctx &ctx, entt::entity owner, bool owned, tween_prop prop, vec4 to,
                   f32 seconds, ease curve, const tween_desc &desc) {
  timer_state &st = ctx.timers;
  if (owned) {
    if (!world(ctx).valid(owner)) {
      NJIN_WARN("tween: entity is not valid");
      return {};
    }
    // A new tween of the same kind on the same entity replaces the old one.
    if (prop != tween_prop::custom)
      for (tween_job &t : st.tweens)
        if (t.owned && t.owner == owner && t.prop == prop)
          t.dead = true;
  }
  tween_job t;
  t.id = st.next_id++;
  t.owner = owner;
  t.owned = owned;
  t.prop = prop;
  t.curve = curve;
  t.desc = desc;
  t.scene = scene_current(ctx).id;
  t.duration = std::max(seconds, 0.0f);
  t.wait = std::max(desc.delay, 0.0f);
  t.runs_left = desc.repeat;
  t.to = to;
  st.tweens.push_back(std::move(t));
  return {st.tweens.back().id};
}
} // namespace

mod_desc timer_module() { return mod_desc{.name = "njin.timer", .setup = setup}; }

timer_handle timer_after(njin_ctx &ctx, f32 seconds, std::function<void(njin_ctx &)> fn,
                         const timer_desc &desc) {
  return timer_every(ctx, seconds, std::move(fn), 1, desc);
}

timer_handle timer_every(njin_ctx &ctx, f32 interval, std::function<void(njin_ctx &)> fn, i32 count,
                         const timer_desc &desc) {
  if (!fn || count == 0)
    return {};
  timer_state &st = ctx.timers;
  timer_job t;
  t.id = st.next_id++;
  t.left = std::max(interval, 0.0f);
  t.interval = interval;
  t.remaining = count < 0 ? -1 : count;
  t.desc = desc;
  t.owned = desc.owner != entt::null;
  t.scene = scene_current(ctx).id;
  t.fn = std::move(fn);
  st.timers.push_back(std::move(t));
  return {st.timers.back().id};
}

void timer_cancel(njin_ctx &ctx, timer_handle timer) {
  for (timer_job &t : ctx.timers.timers)
    if (t.id == timer.id && timer.id != 0)
      t.dead = true;
}

bool timer_active(const njin_ctx &ctx, timer_handle timer) {
  for (const timer_job &t : ctx.timers.timers)
    if (t.id == timer.id && timer.id != 0)
      return !t.dead;
  return false;
}

tween_handle tween_move(njin_ctx &ctx, entt::entity entity, vec2 to, f32 seconds, ease curve,
                        const tween_desc &desc) {
  return start(ctx, entity, true, tween_prop::pos, pack(to), seconds, curve, desc);
}

tween_handle tween_scale(njin_ctx &ctx, entt::entity entity, f32 to, f32 seconds, ease curve,
                         const tween_desc &desc) {
  return start(ctx, entity, true, tween_prop::scale, pack(to), seconds, curve, desc);
}

tween_handle tween_rotate(njin_ctx &ctx, entt::entity entity, f32 to, f32 seconds, ease curve,
                          const tween_desc &desc) {
  return start(ctx, entity, true, tween_prop::rot, pack(to), seconds, curve, desc);
}

tween_handle tween_tint(njin_ctx &ctx, entt::entity entity, rgba to, f32 seconds, ease curve,
                        const tween_desc &desc) {
  return start(ctx, entity, true, tween_prop::tint, pack(to), seconds, curve, desc);
}

tween_handle tween_value(njin_ctx &ctx, f32 from, f32 to, f32 seconds,
                         std::function<void(njin_ctx &, f32)> apply_fn, ease curve,
                         const tween_desc &desc, entt::entity owner) {
  const bool owned = owner != entt::null;
  const tween_handle h = start(ctx, owner, owned, tween_prop::custom, pack(to), seconds, curve, desc);
  for (tween_job &t : ctx.timers.tweens) {
    if (t.id == h.id) {
      t.from = pack(from);
      t.apply = std::move(apply_fn);
    }
  }
  return h;
}

void tween_cancel(njin_ctx &ctx, tween_handle tween) {
  for (tween_job &t : ctx.timers.tweens)
    if (t.id == tween.id && tween.id != 0)
      t.dead = true;
}

void tween_cancel_all(njin_ctx &ctx, entt::entity entity) {
  for (tween_job &t : ctx.timers.tweens)
    if (t.owned && t.owner == entity)
      t.dead = true;
}

bool tween_active(const njin_ctx &ctx, tween_handle tween) {
  for (const tween_job &t : ctx.timers.tweens)
    if (t.id == tween.id && tween.id != 0)
      return !t.dead;
  return false;
}
} // namespace njin
