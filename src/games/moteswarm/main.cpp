#include "game.h"

int main() {
  using namespace njin;
  njin_ctx *ctx = njin_create({.title = "Mote Swarm",
                               .width = 1280.0f,
                               .height = 720.0f,
                               .target_fps = 60.0f,
                               .clear_bg_color = {0.075f, 0.078f, 0.105f, 1.0f},
                               .resizable = true,
                               .app_name = "MoteSwarm",
                               .virtual_size = {1280.0f, 720.0f},
                               .integer_scale = false,
                               .smooth_ui = true});
  njin_mod_register(*ctx, moteswarm::module());
#ifndef NDEBUG
  debug_server_start(*ctx);
#endif
  njin_run(*ctx);
  njin_destroy(ctx);
}
