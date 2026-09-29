#include "hierarchy.h"
#include "_comps.h"
#include "njin_ctx.h"
#include "njin_log.h"
#include <algorithm>
#include <vector>

namespace njin {
namespace {
// Deeper than this is treated as a cycle (a child_of chain that loops back).
constexpr i32 max_depth = 64;

// Number of child_of links above `entity`, or -1 for a cycle.
i32 depth_of(const entt::registry &registry, entt::entity entity) {
  i32 depth = 0;
  const child_of *link = registry.try_get<child_of>(entity);
  while (link != nullptr) {
    if (++depth > max_depth)
      return -1;
    if (!registry.valid(link->parent))
      break;
    link = registry.try_get<child_of>(link->parent);
  }
  return depth;
}

struct pending_child {
  i32 depth = 0;
  entt::entity entity{};
};

void update(context &ctx) {
  entt::registry &registry = world(ctx);
  std::vector<pending_child> children;
  std::vector<entt::entity> cyclic;
  for (const auto [entity, link] : registry.view<const child_of>().each()) {
    const i32 depth = depth_of(registry, entity);
    if (depth < 0)
      cyclic.push_back(entity);
    else
      children.push_back({depth, entity});
  }
  // Break cycles for good, so the warning is logged once. An entity hanging
  // below a cycle is detached too: it has no root to follow.
  for (const entt::entity entity : cyclic) {
    NJIN_WARN("hierarchy: child_of cycle at entity %u, detached",
              (u32)entt::to_integral(entity));
    registry.remove<child_of>(entity);
  }
  // Parents first, so a grandchild combines with its parent's fresh transform.
  // A child whose parent is destroyed here is also seen after it.
  std::stable_sort(children.begin(), children.end(),
                   [](const pending_child &a, const pending_child &b) {
                     return a.depth < b.depth;
                   });

  for (const pending_child &item : children) {
    if (!registry.valid(item.entity))
      continue;
    const child_of link = registry.get<child_of>(item.entity);
    const transform *parent = registry.valid(link.parent)
                                  ? registry.try_get<transform>(link.parent)
                                  : nullptr;
    if (parent == nullptr) {
      if (link.destroy_with_parent)
        registry.destroy(item.entity);
      else
        registry.remove<child_of>(item.entity);
      continue;
    }
    if (transform *tr = registry.try_get<transform>(item.entity))
      *tr = transform_combine(*parent, link.local);
  }
}

void setup(context &ctx) { ecs_register(ctx, phase_post_update, update, "update"); }
} // namespace

mod_desc hierarchy_module() {
  return mod_desc{.name = "njin.hierarchy", .setup = setup};
}
} // namespace njin
