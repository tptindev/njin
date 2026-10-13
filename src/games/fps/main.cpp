#include "fps.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <njin.h>
#include <string>

// njin_fps                          play alone
// njin_fps --host [--port 7777]     open a game and play in it
// njin_fps --join 192.168.1.20      join one (--port if not 7777)
// Online, a bad network is simulated with --lag MS, --jitter MS and --loss %;
// --bot makes the player walk and shoot by itself.
int main(int argc, char **argv) {
  fps::net_mode mode;
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (std::strcmp(arg, "--host") == 0)
      mode.kind = fps::net_mode::host;
    else if (std::strcmp(arg, "--join") == 0 && has_value) {
      mode.kind = fps::net_mode::join;
      mode.address = argv[++i];
    } else if (std::strcmp(arg, "--port") == 0 && has_value)
      mode.port = (njin::u16)std::atoi(argv[++i]);
    else if (std::strcmp(arg, "--lag") == 0 && has_value)
      mode.sim.latency = (njin::f32)std::atof(argv[++i]) / 1000.0f;
    else if (std::strcmp(arg, "--jitter") == 0 && has_value)
      mode.sim.jitter = (njin::f32)std::atof(argv[++i]) / 1000.0f;
    else if (std::strcmp(arg, "--loss") == 0 && has_value)
      mode.sim.loss = (njin::f32)std::atof(argv[++i]) / 100.0f;
    else if (std::strcmp(arg, "--bot") == 0)
      mode.bot = true;
    else
      std::fprintf(stderr, "njin_fps: unknown argument %s\n", arg);
  }
  const bool online = mode.kind != fps::net_mode::solo;
  // Online, two windows side by side fit on one screen.
  const std::string title = mode.kind == fps::net_mode::host   ? "njin fps (chủ phòng)"
                            : mode.kind == fps::net_mode::join ? "njin fps (người chơi)"
                                                               : "njin fps";
  const njin::config cfg{.title = title.c_str(),
                           .width = online ? 960.0f : 1280.0f,
                           .height = online ? 540.0f : 720.0f,
                           .target_fps = 144.0f,
                           .clear_bg_color = {0.17f, 0.17f, 0.18f, 1.0f},
                           .resizable = true,
                           .app_name = "njin fps"};
  njin::context *ctx = njin::create(cfg);
  njin::mod_register(*ctx, fps::fps_module(mode));
#ifndef NDEBUG
  // njin_inspector, in a debug build: its World panel shows the 3D pass. One
  // game per machine can serve it: the one playing alone or hosting.
  if (mode.kind != fps::net_mode::join)
    njin::debug_server_start(*ctx);
#endif
  njin::run(*ctx);
  njin::destroy(ctx);
  return 0;
}
