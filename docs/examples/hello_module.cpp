#include <njin.h>

namespace {
// Một system là hàm thường nhận context&.
void spawn(njin::context &ctx) {
  entt::registry &registry = njin::world(ctx);
  const entt::entity entity = registry.create();
  registry.emplace<njin::transform>(entity);
  NJIN_INFO("đã tạo entity đầu tiên");
}

// setup được gọi đúng một lần khi đăng ký module.
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
  njin::mod_register(*ctx, hello_module()); // trước run
  njin::run(*ctx);
  njin::destroy(ctx);
  return 0;
}
