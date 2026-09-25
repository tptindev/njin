#include "njin_prefab.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_scene.h"

namespace njin {
namespace {
const prefab_slot *slot_of(const prefab_store &store, prefab_handle prefab) {
  if (prefab.id == 0 || prefab.id > store.prefabs.size())
    return nullptr;
  return &store.prefabs[prefab.id - 1];
}
} // namespace

prefab_handle prefab_register(njin_ctx &ctx, const prefab_desc &desc) {
  if (desc.name == nullptr || desc.build == nullptr) {
    NJIN_WARN("prefab_register: name or build is null");
    return prefab_handle{};
  }
  const prefab_handle existing = prefab_find(ctx, desc.name);
  if (existing.id != 0)
    return existing;
  ctx.prefab.prefabs.push_back(prefab_slot{
      .name = desc.name, .build = desc.build, .scene_owned = desc.scene_owned});
  return prefab_handle{.id = (u32)ctx.prefab.prefabs.size()};
}

prefab_handle prefab_find(const njin_ctx &ctx, const char *name) {
  if (name == nullptr)
    return prefab_handle{};
  for (usize i = 0; i < ctx.prefab.prefabs.size(); i++) {
    if (ctx.prefab.prefabs[i].name == name)
      return prefab_handle{.id = (u32)(i + 1)};
  }
  return prefab_handle{};
}

entt::entity prefab_spawn(njin_ctx &ctx, prefab_handle prefab,
                          const transform &at) {
  const prefab_slot *slot = slot_of(ctx.prefab, prefab);
  if (slot == nullptr) {
    NJIN_WARN("prefab_spawn: invalid prefab handle %u", prefab.id);
    return entt::null;
  }
  // Copied: the build function may register prefabs, which can move the
  // vector `slot` points into.
  const prefab_fnc build = slot->build;
  entt::registry &registry = world(ctx);
  const entt::entity entity = registry.create();
  registry.emplace<transform>(entity, at);
  if (const scene_handle scene = scene_current(ctx);
      slot->scene_owned && scene.id != 0)
    registry.emplace<njin::scene_owned>(entity, njin::scene_owned{scene});
  build(ctx, entity);
  return entity;
}

entt::entity prefab_spawn(njin_ctx &ctx, const char *name,
                          const transform &at) {
  const prefab_handle prefab = prefab_find(ctx, name);
  if (prefab.id == 0) {
    NJIN_WARN("prefab_spawn: no prefab named '%s'", name != nullptr ? name : "");
    return entt::null;
  }
  return prefab_spawn(ctx, prefab, at);
}

entt::entity prefab_spawn_child(njin_ctx &ctx, prefab_handle prefab,
                                entt::entity parent, const transform &local) {
  entt::registry &registry = world(ctx);
  const transform *parent_tr =
      registry.valid(parent) ? registry.try_get<transform>(parent) : nullptr;
  if (parent_tr == nullptr) {
    NJIN_WARN("prefab_spawn_child: parent is not a valid entity with a transform");
    return entt::null;
  }
  const transform at = transform_combine(*parent_tr, local);
  const entt::entity entity = prefab_spawn(ctx, prefab, at);
  if (entity != entt::null)
    registry.emplace_or_replace<child_of>(entity,
                                          child_of{.parent = parent, .local = local});
  return entity;
}
} // namespace njin
