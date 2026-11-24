#include "modules/particles3d.h"
#include "modules/render_stats.h"
#include "njin_model.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_shader.h"
#include "njin_texture.h"
#include "njin_rlmath.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace njin {
namespace {
void vertex(std::vector<RenderVertex> &vertices,vec3 point,float u,float v,rgba color) {
  vertices.push_back({point.x,point.y,point.z,u,v,1,{color.r,color.g,color.b,color.a}});
}
RenderBlend blend(blend_mode mode) {
  if(mode==blend_alpha) return RenderBlend::Alpha;
  if(mode==blend_additive) return RenderBlend::Additive;
  if(mode==blend_multiply) return RenderBlend::Multiply;
  throw std::runtime_error("njin 3D particles: this blend mode is not ported");
}
}
void portable_particles3d_draw(context &ctx) {
  auto &state=ctx_ref<particles3d_state>(ctx);auto &store=ctx_ref<model_store>(ctx);
  if(state->bursts.empty()&&state->ribbons.empty()) return;
  if(!store->portable_white) {
    const unsigned char pixel[]{255,255,255,255};store->portable_white=renderer_create_texture(ctx,{1,1,pixel});
  }
  const auto &camera=store->portable_camera;
  const vec3 forward=normalize(camera.target-camera.position),right=normalize(cross(forward,camera.up)),up=cross(right,forward);
  const auto &sun=store->portable_frame_light;
  const auto submit=[&](std::vector<RenderVertex> &vertices,RenderTextureHandle texture,blend_mode mode,int style,int shape,float intensity,shader_handle shader={}) {
    if(vertices.empty()) return;
    MeshDraw draw{};draw.vertices=vertices;draw.texture=texture;draw.depth_write=false;draw.blend=blend(mode);
    draw.linear_sampling=true;draw.wrap_s=draw.wrap_t=RenderWrap::Clamp;
    const auto matrix=MatrixToFloatV(store->portable_view_projection);std::copy_n(matrix.v,16,draw.transform.begin());
    draw.particle_effects={float(style),ctx_ref<time_state>(ctx).elapsed,float(shape),intensity};draw.linear_output=store->portable_hdr_active;
    draw.lighting.direction={sun.direction.x,sun.direction.y,sun.direction.z};draw.lighting.color={sun.color.r,sun.color.g,sun.color.b};
    draw.lighting.ambient={sun.ambient.r,sun.ambient.g,sun.ambient.b};draw.shader=portable_shader_bindings(ctx,shader);
    if(draw.shader&&draw.shader->custom_vertex) throw std::invalid_argument("njin 3D particles: a shader with its own vertex stage draws 2D and draw_instanced only");
    renderer_draw_mesh(ctx,draw);
  };
  std::vector<RenderVertex> vertices;
  for(const auto &burst:state->bursts) {
    const auto &em=burst.look;
    if(burst.style<particle3d_flat||burst.style>particle3d_flame) throw std::invalid_argument("njin 3D particles: invalid style");
    auto texture=store->portable_white;float u0=0,v0=0,u1=1,v1=1,aspect=1;bool image=false;
    if(const auto *slot=texture_slot_of(ctx_ref<texture_store>(ctx),em.texture)) {
      if(!slot->portable) throw std::invalid_argument("njin 3D particles: texture has no portable resource");
      texture=slot->portable;image=true;
      const auto area=texture_area(*slot);
      const auto &resource=ctx_ref<window_guard>(ctx).renderer_resources.textures.at(texture.id);
      const float width=float(resource.cpu.width),height=float(resource.cpu.height);
      auto source=em.source;
      if(source.size.x==0||source.size.y==0) source={{0,0},{area.width,area.height}};
      u0=(area.x+source.pos.x)/width;v0=(area.y+source.pos.y)/height;
      u1=u0+source.size.x/width;v1=v0+source.size.y/height;
      aspect=source.size.x/std::max(source.size.y,.001f);
    }
    std::vector<const particle3d*> order;order.reserve(burst.particles.size());
    for(const auto &p:burst.particles) order.push_back(&p);
    if(em.blend==blend_alpha) std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return dot(a->pos-camera.position,forward)>dot(b->pos-camera.position,forward);});
    vertices.clear();vertices.reserve(order.size()*6);
    for(const auto *particle:order) {
      const auto &p=*particle;const float t=p.age/p.life,size=lerp(em.size_start,em.size_end,t)*p.size*burst.scale;
      const auto color=lerp(em.color_start,em.color_end,t);if(size<=0||color.a<=0) continue;
      const float width=size*std::min(aspect,1.f),height=size*std::min(1/aspect,1.f);
      const float angle=p.rotation*PI/180.f;
      auto side=(right*std::cos(angle)+up*std::sin(angle))*(width*.5f);
      auto vertical=(up*std::cos(angle)-right*std::sin(angle))*(height*.5f);
      vec3 head=p.pos+vertical,tail=p.pos-vertical;
      if(burst.stretch>0) {
        const auto eye=camera.projection==camera3d_orthographic?forward:normalize(p.pos-camera.position);
        auto along=p.velocity-eye*dot(p.velocity,eye);const float run=length(along);
        if(run>1e-6f) {along*=1/run;side=normalize(cross(along,eye))*(width*.5f);head=p.pos+along*(height*.5f);tail=p.pos-along*(height*.5f+run*burst.stretch);}
      }
      const vec3 points[]{tail-side,tail+side,head+side,head-side};
      const float uv[][2]{{u0,v1},{u1,v1},{u1,v0},{u0,v0}};
      for(int i:{0,1,2,0,2,3}) vertex(vertices,points[i],uv[i][0],uv[i][1],color);
      ctx_ref<render_stats>(ctx)->particles++;
    }
    submit(vertices,texture,em.blend,int(burst.style)+1,image?1:int(em.shape),1,burst.shader);
    ctx_ref<render_stats>(ctx)->emitters++;
  }
  for(const auto &record:state->ribbons) {
    if(record.desc.style<ribbon3d_flat||record.desc.style>ribbon3d_glow||!std::isfinite(record.desc.intensity)||record.desc.intensity<0)
      throw std::invalid_argument("njin ribbon: invalid style or intensity");
    const auto *points=state->ribbon_points.data()+record.first;std::vector<vec3> sides(record.count);
    vec3 last=right;
    for(unsigned i=0;i<record.count;++i) {
      const auto tangent=points[std::min(i+1,record.count-1)].pos-points[i?i-1:0].pos;
      const auto eye=camera.projection==camera3d_orthographic?forward:points[i].pos-camera.position;
      const auto across=cross(tangent,eye);if(length_sq(across)>1e-12f) last=normalize(across);
      sides[i]=last*(points[i].width*.5f);
    }
    vertices.clear();
    for(unsigned i=0;i+1<record.count;++i) {
      const vec3 positions[]{points[i].pos-sides[i],points[i].pos+sides[i],points[i+1].pos+sides[i+1],points[i+1].pos-sides[i+1]};
      for(int corner:{0,1,2,0,2,3}) {
        const unsigned at=i+(corner>=2);vertex(vertices,positions[corner],corner==1||corner==2?1.f:0.f,float(at)/(record.count-1),points[at].color);
      }
    }
    submit(vertices,store->portable_white,record.desc.blend,int(record.desc.style)+6,1,record.desc.intensity);
  }
  state->ribbons.clear();state->ribbon_points.clear();
}
}
