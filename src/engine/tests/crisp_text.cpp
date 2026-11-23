#include <njin.h>
#include "../runtime/njin_ctx_get.h"
#include "../runtime/njin_font.h"
#include "../runtime/njin_view.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#endif
namespace {
njin::font_handle alternate;
unsigned frame=0,mode=0;std::string capture;njin::rect viewport;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const auto file=capture+".ttf";std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(njin::font_default_ttf),std::streamsize(njin::font_default_ttf_size));out.close();
  alternate=njin::font_load(ctx,file.c_str(),12,njin::font_pixel);if(!alternate.id) throw std::runtime_error("font snapshot fixture load failed");
}
void ui(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  njin::font_set_default(ctx,{});
  njin::draw_text(ctx,"MMMM",{8,8},12,{1,0,0,1});
  njin::draw_text(ctx,"MMMM",{8,28},12,{1,0,0,1});njin::draw_rect(ctx,{{8,28},{44,20}},{0,0,1,1});
  njin::clip_begin(ctx,{{64,8},{12,18}});njin::draw_text(ctx,"MMMM",{64,8},12,{0,1,0,1});njin::clip_end(ctx);
  njin::draw_text(ctx,"MMMM",{8,55},12,{1,0,0,1});njin::draw_rect(ctx,{{8,55},{44,20}},{0,0,1,.5f});
  njin::draw_offset_push(ctx,{64,55},1.5f);njin::draw_alpha_push(ctx,.5f);njin::draw_text(ctx,"MM",{},8,{1,0,0,1});njin::draw_alpha_pop(ctx);njin::draw_offset_pop(ctx);
  const auto &view=njin::ctx_ref<njin::view_state>(ctx);
  if((mode==0||mode==3)&&view->text_layer.empty()) throw std::runtime_error("crisp text did not defer the screen-space pass");
  if(mode==0||mode==3) {
    bool rejected=false;try {njin::font_set_style(ctx,{},njin::font_pixel);} catch(const std::logic_error &) {rejected=true;}
    if(!rejected) throw std::runtime_error("pending font mutation was accepted");
  }
  njin::font_set_default(ctx,alternate); // Existing queued text must keep the previous fallback font.
  if(frame==0) {viewport=njin::window_viewport(ctx);njin::screenshot(ctx,(capture+"-"+std::to_string(mode)+".png").c_str());}
  if(++frame==3) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=njin::renderer_load_image((capture+"-"+std::to_string(mode)+".png").c_str());
  const auto maximum=[&](njin::rect region,unsigned channel) {
    const auto a=viewport.pos+njin::vec2{region.pos.x*viewport.size.x/128,region.pos.y*viewport.size.y/96};
    const auto b=a+njin::vec2{region.size.x*viewport.size.x/128,region.size.y*viewport.size.y/96};int found=0;
    for(unsigned y=unsigned(a.y);y<unsigned(b.y);++y) for(unsigned x=unsigned(a.x);x<unsigned(b.x);++x) found=std::max(found,int(image.pixels.at((size_t(y)*image.width+x)*4+channel)));
    return found;
  };
  if(maximum({{8,8},{44,18}},0)<(mode==1?160:240)||maximum({{8,28},{44,20}},0)!=0||maximum({{8,28},{44,20}},2)!=255)
    throw std::runtime_error("deferred text visibility/opaque occlusion failed");
  if(maximum({{64,8},{12,18}},1)<(mode==1?160:240)||maximum({{77,8},{35,18}},1)!=0) throw std::runtime_error("deferred text logical scissor failed");
  for(auto area:{njin::rect{{8,55},{44,20}},njin::rect{{64,55},{36,25}}}) {
    const int red=maximum(area,0);if(red<(mode==1?60:115)||red>132) throw std::runtime_error("deferred text alpha/offset/translucent occlusion failed: "+std::to_string(red));
  }
  const auto &atlases=njin::ctx_mut<njin::font_store>(ctx)->fallback.portable_atlases;
  if(atlases.contains(njin::font_px(12*viewport.size.y/96))!=(mode!=1)) throw std::runtime_error("font was not baked at window density");
  if(!njin::ctx_ref<njin::view_state>(ctx)->text_layer.empty()) throw std::runtime_error("deferred text queue was not flushed");
  njin::font_set_default(ctx,{});njin::font_unload(ctx,alternate);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_post_render,ui);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable crisp text regression",.width=384,.height=288,.target_fps=120};cfg.audio=false;cfg.virtual_size={128,96};cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("crisp-text-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(mode=0;mode<4;++mode) {frame=0;cfg.crisp_text=mode!=1&&mode!=2;cfg.smooth_ui=mode==2;cfg.render_scale=mode==3?2:1;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable crisp text",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
