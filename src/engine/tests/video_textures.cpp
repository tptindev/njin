#include <njin.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
unsigned frame=0;std::string capture,asset;float paused_at=0;
njin::video_handle movie,stream;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  if(njin::video_open(ctx,{.path="missing-video.mpg",.audio=false}).id) throw std::runtime_error("missing video accepted");
  movie=njin::video_open(ctx,{.path=asset.c_str(),.play=false,.audio=false});
  stream=njin::video_open(ctx,{.path=asset.c_str(),.loop=true,.audio=false});
  if(!movie.id||!stream.id||njin::video_size(ctx,movie)!=njin::vec2{128,128}||njin::video_framerate(ctx,movie)!=25||njin::video_duration(ctx,movie)<.8f||njin::video_has_audio(ctx,movie)||njin::video_playing(ctx,movie)||!njin::video_playing(ctx,stream))
    throw std::runtime_error("video metadata/playback initialization failed: fps="+std::to_string(njin::video_framerate(ctx,movie))+", duration="+std::to_string(njin::video_duration(ctx,movie)));
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  if(frame<3) {
    const float times[]{.1f,.5f};njin::video_seek(ctx,movie,times[frame%2]);
    njin::video_draw(ctx,movie,{{0,0},{32,32}});
    njin::video_seek(ctx,movie,times[(frame+1)%2]);
    njin::video_draw(ctx,movie,{{40,0},{32,32}});
    njin::video_draw_fit(ctx,movie,{{100,0},{80,64}},{0,1,1,1});
    njin::screenshot(ctx,(capture+"."+std::to_string(frame)+".png").c_str());
  }
  if(frame==12) {njin::video_pause(ctx,stream);paused_at=njin::video_time(ctx,stream);}
  if(frame==16) {
    if(njin::video_playing(ctx,stream)||njin::video_time(ctx,stream)!=paused_at) throw std::runtime_error("paused video kept advancing");
    njin::video_set_loop(ctx,stream,false);njin::video_seek(ctx,stream,.05f);njin::video_play(ctx,stream);
  }
  if(frame==119) {njin::video_draw(ctx,stream,{{200,0},{32,32}});njin::screenshot(ctx,(capture+".finished.png").c_str());}
  if(++frame==120) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const std::array<std::array<int,3>,3> colors{{{255,0,0},{0,255,0},{0,0,255}}};
  for(unsigned f=0;f<3;++f) {
    const auto image=njin::renderer_load_image((capture+"."+std::to_string(f)+".png").c_str());
    const auto check=[&](unsigned x,unsigned y,std::array<int,3> expected) {
      const auto i=(size_t(y)*image.width+x)*4;
      for(unsigned c=0;c<3;++c) if(std::abs(int(image.pixels[i+c])-expected[c])>6) throw std::runtime_error("video decode/upload snapshot pixel mismatch at stage "+std::to_string(f)+" x="+std::to_string(x)+" got="+std::to_string(image.pixels[i])+","+std::to_string(image.pixels[i+1])+","+std::to_string(image.pixels[i+2]));
    };
    check(16,16,colors[f%2]);check(56,16,colors[(f+1)%2]);check(140,32,colors[(f+1)%2]);check(104,32,{0,255,255});
  }
  if(!njin::video_finished(ctx,stream)||njin::video_playing(ctx,stream)||njin::video_frame_count(ctx,stream)<20) throw std::runtime_error("video clock/finish lifecycle failed");
  const auto final=njin::renderer_load_image((capture+".finished.png").c_str());const auto at=(size_t(16)*final.width+216)*4;
  if(final.pixels[at]>6||final.pixels[at+1]>6||final.pixels[at+2]<249) throw std::runtime_error("final decoded video frame was not uploaded");
  const auto image=njin::video_texture(ctx,movie);njin::video_close(ctx,movie);njin::video_close(ctx,movie);njin::video_close(ctx,stream);
  if(njin::texture_size(ctx,image)!=njin::vec2{}||njin::video_texture(ctx,movie).id||njin::video_size(ctx,movie)!=njin::vec2{}) throw std::runtime_error("video close did not invalidate texture");
  const auto stats=njin::renderer_resource_stats(ctx);if(stats.live_targets||stats.live_textures!=1) throw std::runtime_error("video resource leak");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable video texture regression",.width=320,.height=240,.target_fps=60};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    const auto root=std::filesystem::path(argv[0]).parent_path();capture=(root/"captures"/("video-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();asset=(root/"model-test-assets"/"video-colors.mpg").string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable video texture",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
