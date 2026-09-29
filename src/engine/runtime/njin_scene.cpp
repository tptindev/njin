#include "njin_scene.h"
#include "_comps.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_draw.h"
#include "njin_cfg.h"
#include "njin_level_impl.h"
#include "njin_log.h"
#include <vector>

namespace njin {
namespace {
const scene_slot *slot_of(const scene_store &store, scene_handle scene) {
  if (scene.id == 0 || scene.id > store.scenes.size())
    return nullptr;
  return &store.scenes[scene.id - 1];
}

const char *label_of(const scene_store &store, scene_handle scene) {
  const scene_slot *slot = slot_of(store, scene);
  return slot != nullptr ? slot->name.c_str() : "<none>";
}

// A frame this long or longer counts as this long for the fade, so a scene
// whose on_enter blocks for a second still fades in instead of popping.
constexpr f32 fade_max_step = 1.0f / 30.0f;

// Progress of a stage lasting `duration` seconds, after `time` seconds.
f32 progress(f32 time, f32 duration) {
  return duration > 0.0f ? clamp(time / duration, 0.0f, 1.0f) : 1.0f;
}

void advance_fade(scene_store &store, f32 dt_real) {
  scene_fade_state &fade = store.fade;
  const f32 dt = dt_real < fade_max_step ? dt_real : fade_max_step;
  switch (fade.stage) {
  case fade_stage::none:
    return;
  case fade_stage::out:
    fade.time += dt;
    fade.cover = progress(fade.time, fade.transition.fade_out);
    if (fade.cover >= 1.0f) {
      fade.stage = fade_stage::covered;
      fade.time = 0.0f;
    }
    return;
  case fade_stage::covered:
    // The covered frame (with its loading screen) has been drawn: switch now.
    store.pending = fade.target;
    store.has_pending = true;
    fade.stage = fade_stage::hold;
    fade.time = 0.0f;
    return;
  case fade_stage::hold:
    fade.time += dt;
    if (fade.time >= fade.transition.hold) {
      fade.stage = fade_stage::in;
      fade.time = 0.0f;
    }
    return;
  case fade_stage::in:
    fade.time += dt;
    fade.cover = 1.0f - progress(fade.time, fade.transition.fade_in);
    if (fade.cover <= 0.0f)
      fade = scene_fade_state{};
    return;
  }
}

void destroy_owned(entt::registry &registry, scene_handle scene) {
  std::vector<entt::entity> doomed;
  for (const auto [entity, owned] : registry.view<const scene_owned>().each()) {
    if (owned.scene.id == scene.id)
      doomed.push_back(entity);
  }
  registry.destroy(doomed.begin(), doomed.end());
}
} // namespace

scene_handle scene_register(context &ctx, const scene_desc &desc) {
  if (desc.name == nullptr) {
    NJIN_WARN("scene_register: name is null");
    return scene_handle{};
  }
  const scene_handle existing = scene_find(ctx, desc.name);
  if (existing.id != 0)
    return existing;
  ctx.scene.scenes.push_back(scene_slot{
      .name = desc.name, .on_enter = desc.on_enter, .on_exit = desc.on_exit});
  return scene_handle{.id = (u32)ctx.scene.scenes.size()};
}

scene_handle scene_find(const context &ctx, const char *name) {
  if (name == nullptr)
    return scene_handle{};
  for (usize i = 0; i < ctx.scene.scenes.size(); i++) {
    if (ctx.scene.scenes[i].name == name)
      return scene_handle{.id = (u32)(i + 1)};
  }
  return scene_handle{};
}

void scene_set(context &ctx, scene_handle scene) {
  if (slot_of(ctx.scene, scene) == nullptr) {
    NJIN_WARN("scene_set: invalid scene handle %u", scene.id);
    return;
  }
  ctx.scene.pending = scene;
  ctx.scene.has_pending = true;
  ctx.scene.fade = scene_fade_state{};
}

void scene_fade(context &ctx, scene_handle scene,
                const scene_transition &transition) {
  if (slot_of(ctx.scene, scene) == nullptr) {
    NJIN_WARN("scene_fade: invalid scene handle %u", scene.id);
    return;
  }
  scene_fade_state &fade = ctx.scene.fade;
  fade.target = scene;
  fade.transition = transition;
  switch (fade.stage) {
  case fade_stage::none:
    if (scene.id == ctx.scene.current.id)
      return;
    fade.stage = fade_stage::out;
    fade.time = 0.0f;
    fade.cover = 0.0f;
    return;
  case fade_stage::out:
  case fade_stage::covered:
    return;
  case fade_stage::hold:
    // Already switched behind a covered screen: switch again, still covered.
    if (scene.id != ctx.scene.current.id)
      fade.stage = fade_stage::covered;
    return;
  case fade_stage::in:
    // Cover again from where the screen is, without a jump.
    fade.stage = fade_stage::out;
    fade.time = fade.cover * transition.fade_out;
    return;
  }
}

bool scene_transitioning(const context &ctx) {
  return ctx.scene.fade.stage != fade_stage::none;
}

f32 scene_transition_cover(const context &ctx) { return ctx.scene.fade.cover; }

void scene_fade_draw(context &ctx) {
  const scene_fade_state &fade = ctx.scene.fade;
  if (fade.stage == fade_stage::none || fade.cover <= 0.0f)
    return;
  rgba color = fade.transition.color;
  color.a *= fade.cover;
  draw_rect(ctx, rect{{0.0f, 0.0f}, screen_size(ctx)}, color);
  const bool covered = fade.stage == fade_stage::covered || fade.stage == fade_stage::hold;
  if (covered && fade.transition.draw_loading != nullptr)
    fade.transition.draw_loading(ctx);
}

scene_handle scene_current(const context &ctx) { return ctx.scene.current; }

void scene_store_apply(context &ctx) {
  scene_store &store = ctx.scene;
  advance_fade(store, ctx.time.dt_real);
  if (!store.has_pending)
    return;
  // Cleared before the hooks run, so a hook that calls scene_set queues the
  // next switch for the following frame instead of recursing.
  store.has_pending = false;
  const scene_handle target = store.pending;
  const scene_handle old = store.current;
  if (target.id == old.id)
    return;

  NJIN_INFO("scene: %s -> %s", label_of(store, old), label_of(store, target));
  if (const scene_slot *slot = slot_of(store, old);
      slot != nullptr && slot->on_exit != nullptr)
    slot->on_exit(ctx);
  if (old.id != 0) {
    destroy_owned(ctx.ecs.registry, old);
    // The level's entities went with the scene; free its textures too.
    level_store_scene_exit(ctx, old);
  }

  store.current = target;
  if (const scene_slot *slot = slot_of(store, target);
      slot != nullptr && slot->on_enter != nullptr)
    slot->on_enter(ctx);
}
} // namespace njin
