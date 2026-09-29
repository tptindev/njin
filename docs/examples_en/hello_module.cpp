#include <njin.h>

namespace {
// A system is an ordinary function that takes context&.
void spawn(njin::context &ctx) {
  entt::registry &registry = njin::world(ctx);
  const entt::entity entity = registry.create();
  registry.emplace<njin::transform>(entity);
  NJIN_INFO("created the first entity");
}

// setup is called exactly once when the module is registered.
void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
}
} // namespace

njin::mod_desc hello_module() {
  return {.name = "hello", .setup = setup};
}

int main() {
  const njin::config cfg{.title = "hello",
                           .width = 800.0f,
                           .height = 600.0f,
                           .target_fps = 60.0f};

  njin::context *ctx = njin::create(cfg);
  njin::mod_register(*ctx, hello_module()); // before run
  njin::run(*ctx);
  njin::destroy(ctx);
  return 0;
}
