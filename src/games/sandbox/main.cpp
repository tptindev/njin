#include <njin.h>
#include "modules/hello.h"

using njin::debug_server_start;
using njin::config;
using njin::context;
using njin::create;
using njin::destroy;
using njin::mod_register;
using njin::run;

int main() {
  const config cfg{
      .title = "njin sandbox",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = {.r = 1.0, .g = 1.0, .b = 1.0, .a = 1.0}};

  context *ctx = create(cfg);
  mod_register(*ctx, sandbox::hello_module());
#ifndef NDEBUG
  debug_server_start(*ctx); // njin_inspector, in a debug build
#endif
  run(*ctx);
  destroy(ctx);

  return 0;
}
