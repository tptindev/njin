#include <njin.h>
#include "modules/hello.h"

using njin::debug_server_start;
using njin::njin_cfg;
using njin::njin_ctx;
using njin::njin_create;
using njin::njin_destroy;
using njin::njin_mod_register;
using njin::njin_run;

int main() {
  const njin_cfg cfg{
      .title = "njin sandbox",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = {.r = 1.0, .g = 1.0, .b = 1.0, .a = 1.0}};

  njin_ctx *ctx = njin_create(cfg);
  njin_mod_register(*ctx, sandbox::hello_module());
#ifndef NDEBUG
  debug_server_start(*ctx); // njin_inspector, in a debug build
#endif
  njin_run(*ctx);
  njin_destroy(ctx);

  return 0;
}
