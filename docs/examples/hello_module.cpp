#include <njin.h>

namespace {
// Một system là hàm thường nhận njin_ctx&.
void spawn(njin::njin_ctx &ctx) {
  entt::registry &registry = njin::world(ctx);
  const entt::entity entity = registry.create();
  registry.emplace<njin::transform>(entity);
  NJIN_INFO("đã tạo entity đầu tiên");
}

// setup được gọi đúng một lần khi đăng ký module.
void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
}
} // namespace

njin::mod_desc hello_module() {
  return {.name = "hello", .setup = setup};
}

int main() {
  const njin::njin_cfg cfg{.title = "hello",
                           .width = 800.0f,
                           .height = 600.0f,
                           .target_fps = 60.0f};

  njin::njin_ctx *ctx = njin::njin_create(cfg);
  njin::njin_mod_register(*ctx, hello_module()); // trước njin_run
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
  return 0;
}
