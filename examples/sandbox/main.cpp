#include <njin.h>
#include "modules/hello.h"
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <stdexcept>

using njin::debug_server_start;
using njin::config;
using njin::context;
using njin::create;
using njin::destroy;
using njin::mod_register;
using njin::run;

namespace {
int frames_left=0;
void stop_after_frame(context &ctx) { if(--frames_left==0) njin::quit(ctx); }
void setup_frame_limit(context &ctx) { njin::ecs_register(ctx,njin::phase_post_render,stop_after_frame); }
}

int main(int argc,char **argv) {
  try {
  config cfg{
      .title = "njin sandbox",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = {.r = 1.0, .g = 1.0, .b = 1.0, .a = 1.0}};

  for(int i=1;i<argc;++i) {
    const std::string_view arg=argv[i];
    if(arg.starts_with("--renderer=")) {
      if(!njin::parse_graphics_backend(arg.substr(11),cfg.graphics_backend))
        throw std::invalid_argument("renderer must be legacy-opengl, bgfx-opengl, vulkan, d3d12, metal, gles, webgl2 or auto");
    } else if(arg.starts_with("--frames=")) {
      const std::string value(arg.substr(9)); size_t used=0;
      frames_left=std::stoi(value,&used);
      if(used!=value.size() || frames_left<1 || frames_left>12000)
        throw std::invalid_argument("frames must be 1..12000");
    } else throw std::invalid_argument("usage: njin_sandbox [--renderer=legacy-opengl|bgfx-opengl|vulkan|d3d12|auto] [--frames=N]");
  }
  std::unique_ptr<context,void(*)(context*)> ctx(create(cfg),destroy);
  mod_register(*ctx, sandbox::hello_module());
  if(frames_left) mod_register(*ctx,{.name="sandbox.frame_limit",.setup=setup_frame_limit});
#ifndef NDEBUG
  if(!frames_left && cfg.graphics_backend==njin::GraphicsBackend::OpenGL)
    debug_server_start(*ctx); // interactive OpenGL, as before
  else if(!frames_left)
    NJIN_WARN("bgfx sandbox: debug inspector rendering is not ported");
#endif
  run(*ctx);

  return 0;
  } catch(const std::exception &error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
