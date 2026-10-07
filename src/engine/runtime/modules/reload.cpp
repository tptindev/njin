#include "reload.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_path.h"
#include "njin_script_impl.h"

namespace njin {
namespace {
enum class change { none, settled };

// Tracks one file: the first sight records its time; a new time is held
// until a later check sees it unchanged, so a file still being written is
// not read half-way.
change check(reload_state &state, const std::string &path, bool immediate) {
  file_stamp now{};
  if (!file_stamp_of(path, now))
    return change::none; // deleted or being replaced; try again later
  const auto [it, inserted] = state.watches.try_emplace(path);
  reload_watch &w = it->second;
  if (inserted) {
    w.seen = now;
    return change::none;
  }
  if (now == w.seen) {
    w.has_pending = false;
    return change::none;
  }
  if (!immediate && (!w.has_pending || w.pending != now)) {
    w.pending = now;
    w.has_pending = true;
    return change::none;
  }
  w.seen = now;
  w.has_pending = false;
  return change::settled;
}

i32 scan(context &ctx, bool immediate) {
  reload_state &state = ctx.reload;
  entt::dispatcher &dispatcher = events(ctx);
  i32 reloaded = 0;
  for (usize i = 0; i < ctx.texture.slots.size(); i++) {
    const texture_slot &slot = ctx.texture.slots[i];
    if (!slot.alive || slot.path.empty())
      continue;
    const std::string path = slot.path;
    if (check(state, path, immediate) != change::settled)
      continue;
    const bool ok = texture_store_reload(ctx.texture, texture_handle{.id = (u32)(i + 1)});
    NJIN_INFO("hot reload: texture %s%s", path.c_str(), ok ? "" : " (failed)");
    dispatcher.enqueue(asset_reloaded{path, false, ok});
    reloaded += ok ? 1 : 0;
  }
  for (usize i = 0; i < ctx.shader.slots.size(); i++) {
    const shader_slot &slot = ctx.shader.slots[i];
    if (!slot.alive)
      continue;
    // Check both stages every time, so each keeps its own record.
    const std::string vs = slot.vs_path;
    const std::string fs_path = slot.fs_path;
    bool changed = false;
    if (!vs.empty())
      changed = check(state, vs, immediate) == change::settled || changed;
    if (!fs_path.empty())
      changed = check(state, fs_path, immediate) == change::settled || changed;
    if (!changed)
      continue;
    const bool ok = shader_store_reload(ctx.shader, shader_handle{.id = (u32)(i + 1)});
    const std::string &name = fs_path.empty() ? vs : fs_path;
    NJIN_INFO("hot reload: shader %s%s", name.c_str(), ok ? "" : " (failed, old shader kept)");
    dispatcher.enqueue(asset_reloaded{name, true, ok});
    reloaded += ok ? 1 : 0;
  }
  for (const std::string &path : script_watched_files(ctx)) {
    if (check(state, asset_path(path.c_str()), immediate) != change::settled)
      continue;
    const bool ok = script_reload_file(ctx, path);
    NJIN_INFO("hot reload: script %s%s", path.c_str(), ok ? "" : " (failed, old functions kept)");
    dispatcher.enqueue(asset_reloaded{.path = path, .ok = ok, .script = true});
    reloaded += ok ? 1 : 0;
  }
  return reloaded;
}

void poll(context &ctx) {
  reload_state &state = ctx.reload;
  if (!state.enabled)
    return;
  state.timer += ctx.time.dt_real;
  if (state.timer < state.interval)
    return;
  state.timer = 0.0f;
  scan(ctx, false);
}

void setup(context &ctx) { ecs_register(ctx, phase_pre_update, poll, "poll"); }
} // namespace

mod_desc reload_module() { return mod_desc{.name = "njin.reload", .setup = setup}; }

void hot_reload_enable(context &ctx, bool on, f32 interval) {
  reload_state &state = ctx.reload;
  state.enabled = on;
  state.interval = interval > 0.0f ? interval : 0.25f;
  state.timer = 0.0f;
  if (on) {
    // Record current times now, so files edited before enabling do not all
    // reload at once, and the first real edit is noticed promptly.
    for (const texture_slot &slot : ctx.texture.slots)
      if (slot.alive && !slot.path.empty())
        check(state, slot.path, false);
    for (const shader_slot &slot : ctx.shader.slots) {
      if (!slot.alive)
        continue;
      if (!slot.vs_path.empty())
        check(state, slot.vs_path, false);
      if (!slot.fs_path.empty())
        check(state, slot.fs_path, false);
    }
    for (const std::string &path : script_watched_files(ctx))
      check(state, asset_path(path.c_str()), false);
  }
}

bool hot_reload_enabled(const context &ctx) { return ctx.reload.enabled; }

i32 hot_reload_now(context &ctx) { return scan(ctx, true); }
} // namespace njin
