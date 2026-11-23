#include "njin_draw_portable.h"
#include "njin_camera.h"
#include "njin_window.h"
#include "modules/fx.h"
#include "njin_frame_impl.h"
#include "njin_ctx_get.h"
#include "njin_view.h"
#include "njin_texture.h"
#include "njin_shader.h"
#include <cmath>
#include <stdexcept>
#include <vector>
#include <algorithm>
namespace njin {
void draw_portable_slot_textures(const context &ctx,const texture_slot &slot,std::span<const texture_draw_desc> descriptions,bool world_space,std::optional<RenderBlend> blend) {
  const auto min_filter=slot.filter==filter_mipmap?RenderMinFilter::LinearMipLinear:slot.filter==filter_linear?RenderMinFilter::LinearMipNearest:RenderMinFilter::NearestMipNearest;
  if(!slot.packed) {draw_portable_textures(ctx,slot.portable,{float(slot.texture.width),float(slot.texture.height)},descriptions,slot.filter!=filter_nearest,world_space,blend,min_filter);return;}
  std::vector<texture_draw_desc> mapped(descriptions.begin(),descriptions.end());
  for(auto &d:mapped) {
    if(d.source.size.x==0||d.source.size.y==0) d.source={{0,0},{slot.area.width,slot.area.height}};
    d.source.pos+=vec2{slot.area.x,slot.area.y};
  }
  draw_portable_textures(ctx,slot.portable,{float(slot.texture.width),float(slot.texture.height)},mapped,slot.filter!=filter_nearest,world_space,blend,min_filter);
}
void draw_portable_slot_texture(const context &ctx,const texture_slot &slot,const texture_draw_desc &description,bool world_space) {
  draw_portable_slot_textures(ctx,slot,{&description,1},world_space);
}
void draw_portable_vertices(const context &ctx,RenderTextureHandle texture,std::span<const RenderVertex> input,
                           rgba tint,bool linear,bool world_space,std::optional<RenderBlend> blend,RenderMinFilter min_filter) {
  auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  const auto &state=resources.draw_2d;
  const auto &view=ctx_ref<view_state>(ctx);
  const bool virtual_target=view->drawing&&resources.active_target.id==view->portable_target.id;
  auto screen=window_size(ctx);
  if(virtual_target) screen=view_logical_size(view);
  else if(resources.active_target) {
    const auto &target=resources.targets.at(resources.active_target.id);
    screen={(float)target.width,(float)target.height}; world_space=false;
  }
  if(screen.x<=0 || screen.y<=0) return;
  const float canvas=view->canvas_on && (!resources.active_target||virtual_target) ? view->canvas_scale : 1.0f;
  if(view->canvas_on) world_space=false;
  auto camera=camera_active(ctx);
  if(world_space) {
    vec2 offset{}; float rotation=0;
    if(fx_shake_sample(ctx,offset,rotation)) { camera.offset+=offset; camera.rotation+=rotation; }
  }
  const float angle=camera.rotation*3.14159265359f/180,cc=std::cos(angle),ss=std::sin(angle);
  thread_local std::vector<RenderVertex> transformed;
  transformed.assign(input.begin(),input.end());
  for(auto &vertex:transformed) {
    float px=state.offset.x+vertex.x*state.scale,py=state.offset.y+vertex.y*state.scale;
    if(world_space) {
      const float dx=px-camera.target.x,dy=py-camera.target.y;
      px=camera.offset.x+camera.zoom*(dx*cc-dy*ss); py=camera.offset.y+camera.zoom*(dx*ss+dy*cc);
    }
    vertex.x=px*canvas; vertex.y=py*canvas;
    if(view->ui_window&&!resources.active_target) {vertex.x=view->offset.x+vertex.x*view->scale;vertex.y=view->offset.y+vertex.y*view->scale;}
  }
  MeshDraw draw{transformed,{}}; draw.texture=texture; draw.linear_sampling=linear;
  draw.depth_test=false; draw.blend=blend.value_or(state.blend);draw.color_min_filter=min_filter;
  draw.tint={tint.r,tint.g,tint.b,tint.a*state.alpha};
  draw.scissor_enabled=state.clipped; draw.scissor=state.clip;
  if(view->ui_window&&!resources.active_target&&!state.clipped) {
    const auto size=view_logical_size(view)*view->scale;draw.scissor_enabled=true;
    draw.scissor={int(view->offset.x),int(view->offset.y),unsigned(size.x),unsigned(size.y)};
  }
  draw.sprite_effects=state.sprite_effects;
  draw.shader=portable_shader_bindings(ctx,ctx_ref<shader_store>(ctx)->active);
  if(draw.shader&&(draw.sprite_effects.dissolving||draw.sprite_effects.flash[3]>0)) throw std::logic_error("njin sprite FX: custom shader and dissolve/flash cannot be active together");
  draw.transform={2/screen.x,0,0,0,0,-2/screen.y,0,0,0,0,1,0,-1,1,0,1};
  renderer_draw_mesh(const_cast<context&>(ctx),draw);
}
void draw_portable_solid(const context &ctx,std::span<const vec2> points,rgba color) {
  auto &white=ctx_ref<window_guard>(ctx).renderer_resources.draw_2d.white;
  if(!white) {
    const unsigned char rgba[]{255,255,255,255};
    white=renderer_create_texture(const_cast<context&>(ctx),{1,1,rgba});
  }
  thread_local std::vector<RenderVertex> vertices;
  vertices.clear();
  for(auto point:points) vertices.push_back({point.x,point.y,0,0.5f,0.5f});
  draw_portable_vertices(ctx,white,vertices,color,false,ctx_ref<view_state>(ctx)->world_depth>0);
}

void draw_portable_textures(const context &ctx,RenderTextureHandle texture,vec2 image_size,
                           std::span<const texture_draw_desc> descriptions,bool linear,bool world_space,std::optional<RenderBlend> blend,RenderMinFilter min_filter) {
  const float corners[4][2]{{0,0},{1,0},{1,1},{0,1}};
  const int order[6]{0,1,2,0,2,3};
  thread_local std::vector<RenderVertex> vertices;
  size_t first=0;
  while(first<descriptions.size()) {
    vertices.clear();
    const rgba tint=descriptions[first].tint;
    size_t end=first;
    while(end<descriptions.size() && end-first<1024) {
      const auto &desc=descriptions[end];
      if(desc.tint.r!=tint.r || desc.tint.g!=tint.g || desc.tint.b!=tint.b || desc.tint.a!=tint.a) break;
      rect source=desc.source;
      if(source.size.x==0 || source.size.y==0) source={{0,0},image_size};
      if(source.size.x<0 || source.size.y<0) throw std::runtime_error("njin bgfx: negative texture source size is unsupported");
      const bool flip_x=desc.flip_x!=(desc.scale.x<0),flip_y=desc.flip_y!=(desc.scale.y<0);
      const float width=source.size.x*std::abs(desc.scale.x),height=source.size.y*std::abs(desc.scale.y);
      const float angle=desc.rotation*3.14159265359f/180,c=std::cos(angle),s=std::sin(angle);
      for(int i=0;i<6;++i) {
        const auto &corner=corners[order[i]];
        const float x=(corner[0]-desc.origin.x)*width,y=(corner[1]-desc.origin.y)*height;
        float px=desc.pos.x+x*c-y*s,py=desc.pos.y+x*s+y*c;
        vertices.push_back({px,py,0,(source.pos.x+(flip_x ? 1-corner[0]:corner[0])*source.size.x)/image_size.x,
          (source.pos.y+(flip_y ? 1-corner[1]:corner[1])*source.size.y)/image_size.y});
      }
      ++end;
    }
    draw_portable_vertices(ctx,texture,vertices,tint,linear,world_space,blend,min_filter);
    first=end;
  }
}
void draw_portable_texture(const context &ctx,RenderTextureHandle texture,vec2 image_size,
                          const texture_draw_desc &desc,bool linear,bool world_space,std::optional<RenderBlend> blend,RenderMinFilter min_filter) {
  draw_portable_textures(ctx,texture,image_size,{&desc,1},linear,world_space,blend,min_filter);
}
}
