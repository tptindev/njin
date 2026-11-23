#include <njin.h>
#include <memory>
#include <string>
#include <cstdio>

namespace {
int frames=0;
void update(njin::context &ctx) { if(++frames>3) njin::quit(ctx); }
void setup(njin::context &ctx) { njin::ecs_register(ctx,njin::phase_update,update); }
}
int main() {
  try {
    njin::config cfg{.title="Njin unsupported scene test",.width=160,.height=120,.target_fps=60};
    cfg.audio=false; cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
    const auto rejects=[&](auto call,const char *name) {
      try { call(); }
      catch(const std::exception &error) {
        if(std::string(error.what()).find(name)!=std::string::npos) return;
        throw;
      }
      throw std::runtime_error(std::string("unsupported API silently accepted: ")+name);
    };
    rejects([&] { njin::draw_rect(*ctx,{{0,0},{1,1}},{1,1,1,1}); },"outside an active render frame");
    const auto atlas=njin::atlas_create(*ctx,{});njin::atlas_destroy(*ctx,atlas);
    njin::lighting_set(*ctx,{.enabled=true});njin::lighting_set(*ctx,{});

    if(njin::font_load(*ctx,"missing.ttf",16).id) throw std::runtime_error("missing font was accepted");
    rejects([&] { (void)njin::shader_load(*ctx,nullptr,"missing.frag"); },"GLSL shader");
    const auto target=njin::render_texture_load(*ctx,32,32);
    if(!target.id) throw std::runtime_error("render texture adapter failed to allocate");
    njin::render_texture_unload(*ctx,target);
    if(njin::particles_gpu_available(*ctx)!=njin::instancing_available(*ctx)) throw std::runtime_error("GPU particle availability disagrees with device instancing support");
    // bgfx traces in compute shaders (raytrace3d tests the images); invalid settings are refused.
    if(!njin::raytracing_available(*ctx)) throw std::runtime_error("bgfx Vulkan reports no compute ray tracing");
    auto ray_light=njin::light3d{};
    ray_light.ray_traced_shadows=true;ray_light.ray_traced_reflections=true;
    njin::light3d_set(*ctx,ray_light);
    ray_light.shadow_rays=0;
    rejects([&] { njin::light3d_set(*ctx,ray_light); }, "invalid ray traced shadow settings");
    njin::light3d_set(*ctx,njin::light3d{});
    const njin::sdf_part part{};
    rejects([&] { njin::draw_sdf_blend(*ctx,&part,1,.03f,{1,1,1,1}); }, "begin_3d");
    auto material=njin::material3d{};
    material.clay=-1;
    rejects([&] { njin::material3d_set(*ctx,material); }, "invalid clay");
    const auto entity=njin::world(*ctx).create();
    njin::world(*ctx).emplace<njin::transform3d>(entity);
    auto &shape=njin::world(*ctx).emplace<njin::shape3d_render>(entity);
    shape.material.unlit=true; shape.material.clay=-1;
    njin::mod_register(*ctx,{.name="unsupported scene test",.setup=setup});
    njin::run(*ctx);
    std::fprintf(stderr,"legacy 3D scene silently passed Vulkan rendering\n");
    return 1;
  } catch(const std::exception &error) {
    if(std::string(error.what()).find("invalid clay")!=std::string::npos) return 0;
    std::fprintf(stderr,"unexpected failure: %s\n",error.what()); return 1;
  }
}
