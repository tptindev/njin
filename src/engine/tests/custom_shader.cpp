#include <njin.h>
#include <array>
#include <cmath>
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
#include <windows.h>
#endif
namespace {
unsigned frame=0;std::filesystem::path assets,work;std::string capture,original;
njin::shader_handle shader;njin::texture_handle white,red,green;njin::render_texture_handle target;njin::model_handle plane;
struct Probe {std::string path;njin::rect viewport;njin::vec2 logical;};
std::string profile;std::vector<Probe> screenshots;
void require(bool condition,const char *message) {if(!condition) throw std::runtime_error(message);}
void write(const std::filesystem::path &path,const std::string &value) {
  std::ofstream stream(path,std::ios::binary);stream.write(value.data(),std::streamsize(value.size()));stream.close();require(bool(stream),"could not write shader fixture");
  static int revision=0;std::filesystem::last_write_time(path,std::filesystem::file_time_type::clock::now()+std::chrono::seconds(++revision));
}
std::string read(const std::filesystem::path &path) {std::ifstream stream(path,std::ios::binary);require(bool(stream),"missing compiled test shader");return {std::istreambuf_iterator<char>(stream),{}};}
void defaults(njin::context &ctx) {
  njin::shader_set_i32(ctx,shader,"integer",-123456789);njin::shader_set_f32(ctx,shader,"gain",1);
  njin::shader_set_vec2(ctx,shader,"pair",{1,1});njin::shader_set_vec3(ctx,shader,"triplet",{0,0,1});njin::shader_set_vec4(ctx,shader,"color",{1,1,1,1});
  const njin::vec4 array[]{{1,1,1,1},{1,1,1,1}};njin::shader_set_vec4_array(ctx,shader,"array",array,2);
}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  for(const auto &entry:std::filesystem::directory_iterator(assets)) std::filesystem::copy_file(entry.path(),work/entry.path().filename(),std::filesystem::copy_options::overwrite_existing);
  original=read(work/"custom.njshader");
  require(!njin::shader_load(ctx,nullptr,(work/"missing.njshader").string().c_str()).id,"missing shader accepted");
  write(work/"broken.njshader",R"({"version":1,"profiles":{}})");require(!njin::shader_load(ctx,nullptr,(work/"broken.njshader").string().c_str()).id,"missing shader profile accepted");
  require(!njin::shader_load(ctx,nullptr,nullptr).id,"empty shader load succeeded");
  shader=njin::shader_load(ctx,nullptr,(work/"custom.njshader").string().c_str());require(shader.id!=0,"custom shader load failed");
  const auto stage=work/("custom."+profile+".bin");const auto compiled=read(stage);
  auto wrong_link=compiled;wrong_link[4]^=1;write(stage,wrong_link);
  require(!njin::shader_load(ctx,nullptr,(work/"custom.njshader").string().c_str()).id,"incompatible shader varyings accepted");
  auto corrupt=compiled;corrupt[20]=char(255);corrupt[21]=char(255);write(stage,corrupt);
  require(!njin::shader_load(ctx,nullptr,(work/"custom.njshader").string().c_str()).id,"corrupt shader uniform table accepted");
  write(stage,compiled);
  defaults(ctx);bool mismatch=false;try {njin::shader_set_f32(ctx,shader,"pair",1);} catch(const std::invalid_argument &) {mismatch=true;}require(mismatch,"uniform type mismatch accepted");
  mismatch=false;const njin::vec4 too_many[3]{};try {njin::shader_set_vec4_array(ctx,shader,"array",too_many,3);}catch(const std::invalid_argument &) {mismatch=true;}require(mismatch,"oversized uniform array accepted");
  const unsigned char w[]{255,255,255,255},r[]{255,0,0,255},g[]{0,255,0,255};white=njin::texture_from_pixels(ctx,w,1,1);red=njin::texture_from_pixels(ctx,r,1,1);green=njin::texture_from_pixels(ctx,g,1,1);
  target=njin::render_texture_load(ctx,16,16);
  const njin::vec3 positions[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};const njin::u32 indices[]{0,1,2,0,2,3};
  plane=njin::model_create(ctx,{.positions=positions,.vertex_count=4,.indices=indices,.index_count=6});auto material=njin::model_material_get(ctx,plane,0);material.shader=shader;material.double_sided=true;material.surface.unlit=true;njin::model_material_set(ctx,plane,0,material);
  njin::hot_reload_now(ctx); // Establish package and binary timestamps.
}
void update(njin::context &ctx) {
  if(frame==1) {write(work/"custom.njshader","{");require(njin::hot_reload_now(ctx)==0,"broken package reload succeeded");}
  if(frame==2) {
    write(work/"custom.njshader",original);require(njin::hot_reload_now(ctx)==1,"package reload failed");
    write(work/("custom."+profile+".bin"),"not a compiled shader");require(njin::hot_reload_now(ctx)==0,"corrupt binary reload succeeded");
  }
  if(frame==3) {write(work/("custom."+profile+".bin"),read(assets/("custom."+profile+".bin")));require(njin::hot_reload_now(ctx)==1,"binary reload failed");}
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  njin::shader_set_vec4(ctx,shader,"color",{1,1,1,1});
  njin::render_texture_begin(ctx,target,{0,0,1,1});
  njin::shader_set_texture(ctx,shader,"extra",target);njin::shader_begin(ctx,shader);
  bool feedback=false;try {njin::draw_rect(ctx,{{0,0},{16,16}},{1,1,1,1});}catch(const std::logic_error &) {feedback=true;}njin::shader_end(ctx);require(feedback,"custom sampler feedback accepted");njin::render_texture_end(ctx);
  njin::shader_set_texture(ctx,shader,"extra",red);njin::shader_set_f32(ctx,shader,"gain",.5f);
  njin::texture_set_shader(ctx,white,shader);njin::texture_draw_ex(ctx,white,{.pos={10,10},.scale={32,32}});
  bool busy=false;try {njin::shader_unload(ctx,shader);}catch(const std::logic_error &) {busy=true;}require(busy,"unload of submitted shader accepted");
  njin::shader_set_texture(ctx,shader,"extra",green);njin::shader_set_f32(ctx,shader,"gain",1);njin::texture_draw_ex(ctx,white,{.pos={50,10},.scale={32,32}});
  njin::shader_set_texture(ctx,shader,"extra",target);njin::shader_begin(ctx,shader);njin::draw_rect(ctx,{{90,10},{32,32}},{1,1,1,1});njin::shader_end(ctx);
  njin::texture_set_shader(ctx,white,{});njin::texture_draw_ex(ctx,white,{.pos={130,10},.scale={32,32}});
  auto light=njin::light3d_get(ctx);light.hdr=false;light.shadows=false;njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={0,0,5};camera.target={};camera.projection=njin::camera3d_orthographic;camera.ortho_height=4;camera.entities=false;
  njin::shader_set_texture(ctx,shader,"extra",white);njin::shader_set_vec4(ctx,shader,"color",{1,0,0,1});
  njin::begin_3d(ctx,camera);njin::draw_model(ctx,plane,{.position={-.7f,0,0},.scale={.4f,.4f,.4f}});
  njin::shader_set_vec4(ctx,shader,"color",{0,0,1,1});njin::draw_model(ctx,plane,{.position={.7f,0,0},.scale={.4f,.4f,.4f}});njin::end_3d(ctx);
}
void post(njin::context &ctx) {const auto file=capture+"."+std::to_string(frame)+".png";screenshots.push_back({file,njin::window_viewport(ctx),njin::screen_size(ctx)});njin::screenshot(ctx,file.c_str());if(++frame==4) njin::quit(ctx);}
void shutdown(njin::context &ctx) {
  for(const auto &probe:screenshots) {
    const auto &path=probe.path;const auto image=njin::renderer_load_image(path.c_str());
    const auto pixel=[&](unsigned x,unsigned y,std::array<int,3> expected){const auto px=unsigned(probe.viewport.pos.x+(x+.5f)*probe.viewport.size.x/probe.logical.x),py=unsigned(probe.viewport.pos.y+(y+.5f)*probe.viewport.size.y/probe.logical.y);const auto at=(size_t(py)*image.width+px)*4;for(unsigned c=0;c<3;++c) if(std::abs(int(image.pixels[at+c])-expected[c])>3) throw std::runtime_error("custom shader pixel mismatch "+path+" at "+std::to_string(x)+","+std::to_string(y)+": "+std::to_string(image.pixels[at])+","+std::to_string(image.pixels[at+1])+","+std::to_string(image.pixels[at+2]));};
    pixel(25,25,{128,0,0});pixel(65,25,{0,255,0});pixel(105,25,{0,0,255});pixel(145,25,{255,255,255});pixel(118,120,{0,0,255});pixel(202,120,{0,0,255});
  }
  njin::model_unload(ctx,plane);njin::shader_unload(ctx,shader);njin::shader_unload(ctx,shader);njin::render_texture_unload(ctx,target);njin::texture_unload(ctx,white);njin::texture_unload(ctx,red);njin::texture_unload(ctx,green);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_post_render,post);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable custom shader regression",.width=320,.height=240,.target_fps=120};bool mobile=false;cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;else if(std::strcmp(argv[i],"--mobile")==0) mobile=true;
    if(mobile) cfg.virtual_size={320,240};
    profile=cfg.graphics_backend==njin::GraphicsBackend::Vulkan?"spirv":cfg.graphics_backend==njin::GraphicsBackend::BgfxOpenGL?"glsl":cfg.graphics_backend==njin::GraphicsBackend::OpenGLES?"essl":"dx11";
    const auto root=std::filesystem::absolute(argv[0]).parent_path();assets=root/"shader-test-assets";work=root/"captures"/("shader-assets-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)));std::filesystem::create_directories(work);capture=(root/"captures"/("custom-shader-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();
    for(unsigned repeat=0;repeat<2;++repeat) {frame=0;screenshots.clear();std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable custom shader",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
