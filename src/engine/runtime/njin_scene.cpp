#include "njin_scene.h"
#include "_comps.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
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

void destroy_owned(entt::registry &registry, scene_handle scene) {
  std::vector<entt::entity> doomed;
  for (const auto [entity, owned] : registry.view<const scene_owned>().each()) {
    if (owned.scene.id == scene.id)
      doomed.push_back(entity);
  }
  registry.destroy(doomed.begin(), doomed.end());
}
} // namespace

scene_handle scene_register(njin_ctx &ctx, const scene_desc &desc) {
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

scene_handle scene_find(const njin_ctx &ctx, const char *name) {
  if (name == nullptr)
    return scene_handle{};
  for (usize i = 0; i < ctx.scene.scenes.size(); i++) {
    if (ctx.scene.scenes[i].name == name)
      return scene_handle{.id = (u32)(i + 1)};
  }
  return scene_handle{};
}

void scene_set(njin_ctx &ctx, scene_handle scene) {
  if (slot_of(ctx.scene, scene) == nullptr) {
    NJIN_WARN("scene_set: invalid scene handle %u", scene.id);
    return;
  }
  ctx.scene.pending = scene;
  ctx.scene.has_pending = true;
}

scene_handle scene_current(const njin_ctx &ctx) { return ctx.scene.current; }

void scene_store_apply(njin_ctx &ctx) {
  scene_store &store = ctx.scene;
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
  if (old.id != 0)
    destroy_owned(ctx.ecs.registry, old);

  store.current = target;
  if (const scene_slot *slot = slot_of(store, target);
      slot != nullptr && slot->on_enter != nullptr)
    slot->on_enter(ctx);
}
} // namespace njin
