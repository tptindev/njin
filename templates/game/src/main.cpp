#include <njin.h>

namespace {
void render(njin::context &ctx) {
  njin::draw_text(ctx, "My Game", {40.0f, 40.0f}, 32.0f,
                  {1.0f, 1.0f, 1.0f, 1.0f});
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_render, render, "game.render");
}
} // namespace

int main() {
  const njin::config cfg{.title = "My Game",
                         .width = 960.0f,
                         .height = 540.0f,
                         .target_fps = 60.0f,
                         .clear_bg_color = {0.06f, 0.07f, 0.1f, 1.0f}};
  njin::context *ctx = njin::create(cfg);
  njin::mod_register(*ctx, {.name = "game", .setup = setup});
  njin::run(*ctx);
  njin::destroy(ctx);
  return 0;
}
