#include "hello.h"
#include <njin.h>

namespace sandbox {
namespace {

void spawn(njin::context &ctx) {
  entt::registry &registry = njin::world(ctx);
  const entt::entity entity = registry.create();
  registry.emplace<njin::transform>(entity);
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
}

} // namespace

njin::mod_desc hello_module() {
  return {.name = "hello", .setup = setup};
}

} // namespace sandbox
