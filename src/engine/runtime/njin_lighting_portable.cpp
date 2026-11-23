#include "modules/lighting_internal.h"
#include "modules/render_stats.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_camera.h"
#include "njin_ecs.h"
#include "njin_view.h"
#include "_comps.h"
#include "njin_texture.h"
#include "modules/sprite.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace njin {
namespace {
constexpr int edge_buckets=32;
constexpr float pi=3.14159265359f;

RenderTextureHandle edge_table(context &ctx,lighting_state &state,const light_2d &light,vec2 center,
                               vec2 direction,rect view,RenderPostProcess &post) {
  const bool sun=light.kind==light_directional;
  const float reach=sun?state.desc.shadow_reach:light.radius;
  const float penumbra=sun?light.size*reach/600.f:light.size;
  const rect area=sun?rect{view.pos-vec2{reach+penumbra,reach+penumbra},
                           view.size+vec2{2*(reach+penumbra),2*(reach+penumbra)}}:
                      rect{center-vec2{reach+penumbra,reach+penumbra},vec2{2*(reach+penumbra),2*(reach+penumbra)}};
  std::vector<light_impl::occluder_edge> edges;
  light_impl::gather_edges(ctx,state,edges,{area},std::max(state.desc.occluder_lod,0.f)/post.legacy[6][2]);
  if(edges.empty()) return {};

  const vec2 axis{-direction.y,direction.x};
  const auto project=[axis](vec2 p){return axis.x*p.x+axis.y*p.y;};
  const float low=project(view.pos)+std::min(0.f,axis.x*view.size.x)+std::min(0.f,axis.y*view.size.y);
  const float span=std::abs(axis.x)*view.size.x+std::abs(axis.y)*view.size.y;
  const float inv=float(edge_buckets)/std::max(span,.001f);
  const auto strip=[&](float p){return int(std::clamp(std::floor((p-low)*inv),0.f,float(edge_buckets-1)));};
  const auto angle_bin=[](float a){return int(std::floor((a+pi)*float(edge_buckets)/(2*pi)));};
  std::array<std::vector<size_t>,edge_buckets> buckets;
  auto &gpu=ctx_ref<window_guard>(ctx).graphics;
  const unsigned width=std::min(256u,gpu->max_texture_size());
  const size_t limit=std::min(size_t(width)*gpu->max_texture_size(),size_t(1)<<20);
  size_t texels=1+edge_buckets;
  for(size_t i=0;i<edges.size();++i) {
    const auto &edge=edges[i];
    if(!sun&&edge.solid&&light_impl::inside_owner(edges,edge,center)) continue;
    int first=0,last=edge_buckets-1;
    if(sun) {
      const float a=project(edge.a),b=project(edge.b);
      first=strip(std::min(a,b)-penumbra-.001f);last=strip(std::max(a,b)+penumbra+.001f);
    } else {
      const auto a=edge.a-center,b=edge.b-center,line=b-a;
      const float length_sq=line.x*line.x+line.y*line.y;
      const float t=std::clamp(-(a.x*line.x+a.y*line.y)/std::max(length_sq,1e-8f),0.f,1.f);
      const float nearest=std::hypot(a.x+t*line.x,a.y+t*line.y);
      // Every ray to the sampled emitter crosses a capsule of this radius
      // around a blocking edge when projected back to a ray to the centre.
      if(nearest>penumbra+.001f) {
        const float start=std::atan2(a.y,a.x),end=start+std::remainder(std::atan2(b.y,b.x)-start,2*pi);
        const float padding=std::asin(std::clamp(penumbra/nearest,0.f,1.f))+.0001f;
        first=angle_bin(std::min(start,end)-padding);last=angle_bin(std::max(start,end)+padding);
        if(last-first>=edge_buckets) {first=0;last=edge_buckets-1;}
      }
    }
    texels+=size_t(last-first+1);
    if(texels>limit) throw std::length_error("njin 2D lighting: edge table exceeds texture budget");
    for(int bin=first;bin<=last;++bin) {
      auto &entries=buckets[(bin%edge_buckets+edge_buckets)%edge_buckets];
      if(entries.size()==4096) throw std::length_error("njin 2D lighting: shadow bucket exceeds 4096 edges");
      entries.push_back(i);
    }
  }
  if(texels==1+edge_buckets) return {};
  const unsigned height=unsigned((texels+width-1)/width);
  std::vector<std::array<float,4>> data(size_t(width)*height);
  data[0]={axis.x,axis.y,low,inv};
  size_t cursor=1+edge_buckets;
  for(int bin=0;bin<edge_buckets;++bin) {
    data[1+bin]={float(cursor),float(buckets[bin].size()),0,0};
    for(auto i:buckets[bin]) {
      const auto &edge=edges[i];data[cursor++]={edge.a.x,edge.a.y,edge.b.x,edge.b.y};
    }
  }
  const RenderImage image{width,height,{reinterpret_cast<const u8*>(data.data()),data.size()*sizeof(data[0])},RenderImageFormat::RGBA32F};
  auto available=std::find_if(state.portable_edge_tables.begin(),state.portable_edge_tables.end(),[&](auto handle){return !gpu->texture_in_use(handle);});
  if(available==state.portable_edge_tables.end()) {
    auto &handle=state.portable_edge_tables.emplace_back();
    try {handle=renderer_create_texture(ctx,image);}
    catch(...) {state.portable_edge_tables.pop_back();throw;}
    available=std::prev(state.portable_edge_tables.end());
  } else renderer_update_texture(ctx,*available,image);
  post.radius=float(width);post.samples=float(height);
  return *available;
}

RenderMaterialMaps gbuffers(context &ctx,lighting_state &state,unsigned width,unsigned height,const camera_view &camera,vec2 logical,RenderPostProcess &post) {
  auto &registry=world(ctx);auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  std::vector<entt::entity> sprites;bool maps=false,pixels=false;
  for(auto [entity,tr,spr]:registry.view<const transform,const sprite>().each()) if(spr.visible) {
    sprites.push_back(entity);maps|=spr.normal.id||spr.material.id||spr.emissive.id;pixels|=registry.all_of<light_occluder_pixels>(entity);
  }
  RenderMaterialMaps result;if(!maps&&!pixels) return result;
  const auto &sorted=ctx_ref<sprite_cache>(ctx)->y_sorted;
  std::stable_sort(sprites.begin(),sprites.end(),[&](auto a,auto b) {
    const auto &sa=registry.get<sprite>(a),&sb=registry.get<sprite>(b);
    if(sa.layer!=sb.layer) return sa.layer<sb.layer;
    if(sorted.contains(sa.layer)) return registry.get<transform>(a).pos.y+sa.sort_offset<registry.get<transform>(b).pos.y+sb.sort_offset;
    return entt::to_integral(a)<entt::to_integral(b);
  });
  if(!state.portable_white) {const unsigned char white[]{255,255,255,255};state.portable_white=renderer_create_texture(ctx,{1,1,white});}
  const float a=camera.rotation*3.14159265359f/180,cc=std::cos(a),ss=std::sin(a);
  const auto ensure=[&](unsigned i,unsigned w,unsigned h) {
    auto &handle=state.portable_gbuffers[i];if(handle) {const auto &t=resources.targets.at(handle.id);if(t.width==w&&t.height==h) return;renderer_destroy_target(ctx,handle);handle={};}
    handle=i==2?renderer_create_hdr_target(ctx,w,h):renderer_create_target(ctx,w,h);
  };
  const auto slot=[&](texture_handle handle)->const texture_slot * {
    const auto *s=texture_slot_of(ctx_ref<texture_store>(ctx),handle);
    if(handle.id&&(!s||!s->portable)) throw std::invalid_argument("njin 2D lighting: invalid map/mask texture");
    return s;
  };
  const auto raster=[&](unsigned kind,vec2 extent,float margin) {
    for(auto entity:sprites) {
      const auto &spr=registry.get<sprite>(entity);const auto &tr=registry.get<transform>(entity);
      for(float value:{tr.pos.x,tr.pos.y,tr.rot,tr.scale,spr.emissive_power,spr.tint.a})
        if(!std::isfinite(value)) throw std::invalid_argument("njin 2D lighting: non-finite sprite transform/material");
      if(spr.emissive_power<0||spr.tint.a<0||spr.tint.a>1) throw std::invalid_argument("njin 2D lighting: invalid sprite emission/alpha");
      const auto *base=slot(spr.texture);if(!base) continue;
      const auto *pixel=registry.try_get<light_occluder_pixels>(entity);if(kind==3&&!pixel) continue;
      const auto id=kind==0?spr.normal:kind==1?spr.material:kind==2?spr.emissive:pixel->mask.id?pixel->mask:spr.texture;
      const auto *map=slot(id);const auto base_area=texture_area(*base);const auto map_area=map?texture_area(*map):Rectangle{0,0,1,1};
      auto source=spr.source;if(source.size.x==0||source.size.y==0) source={{0,0},{base_area.width,base_area.height}};
      const float r=tr.rot*3.14159265359f/180,c=std::cos(r),s=std::sin(r);
      std::array<RenderVertex,6> vertices{};const int order[]{0,1,2,0,2,3};const vec2 corners[]{{0,0},{1,0},{1,1},{0,1}};
      for(unsigned i=0;i<6;++i) {
        const auto corner=corners[order[i]];const auto p=(corner-spr.origin)*source.size*std::abs(tr.scale);
        const vec2 world=tr.pos+vec2{p.x*c-p.y*s,p.x*s+p.y*c},delta=world-camera.target;
        const vec2 screen=camera.offset+vec2{delta.x*cc-delta.y*ss,delta.x*ss+delta.y*cc}*camera.zoom+vec2{margin,margin};
        const vec2 uv=source.pos+vec2{(spr.flip_x!=(tr.scale<0))?1-corner.x:corner.x,(spr.flip_y!=(tr.scale<0))?1-corner.y:corner.y}*source.size;
        vertices[i]={screen.x/extent.x*2-1,1-screen.y/extent.y*2,0,map?(map_area.x+uv.x)/map->texture.width:.5f,map?(map_area.y+uv.y)/map->texture.height:.5f};
        vertices[i].uv1={(base_area.x+uv.x)/base->texture.width,(base_area.y+uv.y)/base->texture.height};
      }
      MeshDraw draw{};draw.vertices=vertices;draw.texture=map?map->portable:state.portable_white;draw.post.auxiliary=base->portable;
      draw.depth_test=false;draw.depth_write=false;draw.wrap_s=draw.wrap_t=RenderWrap::Clamp;draw.linear_sampling=map&&map->filter!=filter_nearest;
      draw.post.mode=RenderPostMode::LightGBuffer;draw.post.legacy[0]={float(kind),map?1.f:0.f,spr.emissive_power,spr.tint.a};
      draw.post.legacy[1]={c,s,spr.flip_x?-1.f:1.f,spr.flip_y?-1.f:1.f};renderer_draw_mesh(ctx,draw);
    }
  };
  if(maps) {
    const std::array<float,4> clears[]{{.5f,.5f,1,1},{0,.8f,1,1},{0,0,0,1}};
    for(unsigned kind=0;kind<3;++kind) {ensure(kind,width,height);renderer_begin_target(ctx,state.portable_gbuffers[kind],clears[kind]);raster(kind,logical,0);renderer_end_target(ctx);}
    result.normal.texture=renderer_target_texture(ctx,state.portable_gbuffers[0]);result.metal_rough.texture=renderer_target_texture(ctx,state.portable_gbuffers[1]);result.emission.texture=renderer_target_texture(ctx,state.portable_gbuffers[2]);
  }
  if(pixels) {
    const float margin=std::ceil(state.desc.occluder_margin*camera.zoom);const vec2 extent=logical+vec2{margin*2,margin*2};
    const unsigned w=std::max(1u,unsigned(extent.x*width/logical.x)),h=std::max(1u,unsigned(extent.y*height/logical.y));
    ensure(3,w,h);renderer_begin_target(ctx,state.portable_gbuffers[3]);raster(3,extent,margin);renderer_end_target(ctx);
    result.occlusion.texture=renderer_target_texture(ctx,state.portable_gbuffers[3]);
    post.legacy[8]={margin,margin,extent.x,extent.y};post.legacy[9]={state.desc.pixel_alpha,float(w)/extent.x,std::clamp(float(state.desc.shadow_columns?state.desc.shadow_columns:2048),64.f,4096.f),0};
  }
  return result;
}
}
void portable_lighting_validate(const lighting_desc &d) {
  for(float x:{d.ambient.r,d.ambient.g,d.ambient.b,d.exposure,d.scale,d.shadow_reach,d.occluder_lod,d.pixel_alpha,d.occluder_margin})
    if(!std::isfinite(x)||x<0) throw std::invalid_argument("njin 2D lighting: invalid finite/range setting");
  if(d.scale<.25f||d.scale>1||d.tonemap<tonemap_shoulder||d.tonemap>tonemap_aces||d.pixel_alpha>1)
    throw std::invalid_argument("njin 2D lighting: invalid scale, tone map or alpha threshold");
}
void portable_lighting_world(context &ctx) {
  auto &state=ctx_ref<lighting_state>(ctx);if(!state->desc.enabled) return;
  auto &view=ctx_ref<view_state>(ctx);auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  if(!view->drawing||resources.active_target.id!=view->portable_target.id)
    throw std::logic_error("njin 2D lighting: enable before phase_render and close explicit render targets");
  const auto &d=state->desc;const unsigned w=std::max(1u,unsigned(view->portable_width*d.scale)),h=std::max(1u,unsigned(view->portable_height*d.scale));
  if(state->portable_hdr&&(w!=state->portable_width||h!=state->portable_height)) {
    renderer_destroy_target(ctx,state->portable_hdr);state->portable_hdr={};
  }
  if(!state->portable_hdr) {state->portable_hdr=renderer_create_hdr_target(ctx,w,h);state->portable_width=w;state->portable_height=h;}
  const auto camera=camera_active(ctx);const auto logical=view_logical_size(view);
  const float angle=camera.rotation*3.14159265359f/180;
  RenderPostProcess post;post.texel={1.f/w,1.f/h};
  post.legacy[0]={d.ambient.r,d.ambient.g,d.ambient.b,d.exposure};
  post.legacy[5]={camera.target.x,camera.target.y,camera.offset.x,camera.offset.y};
  post.legacy[6]={std::cos(angle),std::sin(angle),camera.zoom,0};
  post.legacy[7]={logical.x,logical.y,d.shadow_reach,float(d.tonemap)};
  constexpr RenderVertex quad[]{{-1,1,0,0,0},{1,1,0,1,0},{1,-1,0,1,1},{-1,1,0,0,0},{1,-1,0,1,1},{-1,-1,0,0,1}};
  RenderMaterialMaps buffers;
  const auto draw=[&](RenderTextureHandle source,const RenderPostProcess &p,RenderBlend blend) {
    MeshDraw packet{};packet.vertices=quad;packet.texture=source;packet.post=p;packet.depth_test=false;packet.blend=blend;
    packet.maps=buffers;packet.opaque_alpha=true;packet.linear_sampling=true;packet.wrap_s=packet.wrap_t=RenderWrap::Clamp;renderer_draw_mesh(ctx,packet);
  };
  const auto source=renderer_target_texture(ctx,view->portable_target);
  renderer_end_target(ctx);
  try {
    buffers=gbuffers(ctx,state,w,h,camera,logical,post);
    renderer_begin_target(ctx,state->portable_hdr);post.mode=RenderPostMode::LightAmbient;draw(source,post,RenderBlend::Opaque);
    post.auxiliary_linear=false;
    unsigned count=0;
    for(auto [entity,tr,l]:world(ctx).view<const transform,const light_2d>().each()) {
      if(!l.enabled||l.intensity<=0) continue;
      if(++count>64) throw std::runtime_error("njin 2D lighting: exceeds 64 lights in this view");
      for(float x:{tr.pos.x,tr.pos.y,tr.rot,l.intensity,l.radius,l.size,l.height,l.elevation,l.angle,l.cone,l.softness,l.temperature,l.color.r,l.color.g,l.color.b})
        if(!std::isfinite(x)) throw std::invalid_argument("njin 2D lighting: non-finite light");
      if(l.radius<=0||l.height<=0||l.size<0||l.kind<light_point||l.kind>light_directional||l.falloff<falloff_physical||l.falloff>falloff_none)
        throw std::invalid_argument("njin 2D lighting: invalid light");
      auto color=l.color;if(l.temperature>0) {const auto k=light_color_kelvin(l.temperature);color.r*=k.r;color.g*=k.g;color.b*=k.b;}
      const float a=(tr.rot+l.angle)*3.14159265359f/180,half=std::clamp(l.cone,1.f,179.f)*3.14159265359f/360;
      post.legacy[1]={tr.pos.x,tr.pos.y,l.radius,l.height};post.legacy[2]={color.r,color.g,color.b,l.intensity};
      post.legacy[3]={std::cos(a),std::sin(a),std::cos(half),std::cos(half*(1-std::clamp(l.softness,0.f,1.f)))+.001f};
      post.legacy[4]={float(l.kind),float(l.falloff),l.size,std::clamp(l.elevation,1.f,90.f)*3.14159265359f/180};
      post.auxiliary=l.cast_shadows?edge_table(ctx,*state,l,tr.pos,{std::cos(a),std::sin(a)},camera_bounds(ctx),post):RenderTextureHandle{};
      post.legacy[6][3]=post.auxiliary?-1.f:0.f;
      post.legacy[9][3]=l.cast_shadows?1.f:0.f;
      post.mode=RenderPostMode::LightDirect;draw(source,post,RenderBlend::Additive);
      ctx_ref<render_stats>(ctx)->lights++;
    }
    renderer_end_target(ctx);renderer_resume_target(ctx,view->portable_target);
    post.auxiliary={};post.mode=RenderPostMode::LightResolve;draw(renderer_target_texture(ctx,state->portable_hdr),post,RenderBlend::Opaque);
  } catch(...) {
    if(resources.active_target&&resources.active_target.id!=view->portable_target.id) try {renderer_end_target(ctx);} catch(...) {}
    if(!resources.active_target) try {renderer_resume_target(ctx,view->portable_target);} catch(...) {}
    throw;
  }
}
}
