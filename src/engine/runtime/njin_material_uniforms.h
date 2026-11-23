#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
#include <array>
#include <cmath>
#include <stdexcept>
namespace njin::rhi {
inline auto material_maps(const MeshDraw &draw) {
  return std::array<const RenderTextureMap*,4>{&draw.maps.normal,&draw.maps.metal_rough,&draw.maps.occlusion,&draw.maps.emission};
}
inline auto advanced_material_maps(const MeshDraw &draw) {
  return std::array<const RenderTextureMap*,4>{&draw.maps.height,&draw.maps.lightmap,&draw.maps.under,&draw.maps.under_normal};
}
inline auto clearcoat_material_maps(const MeshDraw &draw) {
  return std::array<const RenderTextureMap*,3>{&draw.maps.clearcoat,&draw.maps.clearcoat_roughness,&draw.maps.clearcoat_normal};
}
inline auto all_material_maps(const MeshDraw &draw) {
  return std::array<const RenderTextureMap*,13>{&draw.maps.normal,&draw.maps.metal_rough,&draw.maps.occlusion,&draw.maps.emission,
    &draw.maps.height,&draw.maps.lightmap,&draw.maps.under,&draw.maps.under_normal,&draw.maps.transmission,&draw.maps.thickness,
    &draw.maps.clearcoat,&draw.maps.clearcoat_roughness,&draw.maps.clearcoat_normal};
}
inline bool same_texture_binding(const RenderTextureMap &a,const RenderTextureMap &b) {
  return a.texture.id==b.texture.id&&a.uv.set==b.uv.set&&a.uv.values==b.uv.values&&a.linear==b.linear&&
         a.wrap_s==b.wrap_s&&a.wrap_t==b.wrap_t&&a.min_filter==b.min_filter;
}
// Clearcoat maps reuse the post auxiliary/secondary samplers (8, 9), which a
// built-in mesh draw only needs for transmission and water. Identical bindings
// share one sampler. select = sampler (1 or 2, 0 unused) for coat R, roughness G
// and coat normal, then the coat normal scale.
struct ClearcoatBinding {
  std::array<const RenderTextureMap*,2> slots{};
  std::array<float,4> select{0,0,0,1};
  bool any=false;
  explicit ClearcoatBinding(const MeshDraw &draw) {
    const auto maps=clearcoat_material_maps(draw);
    for(unsigned i=0;i<3;++i) {
      if(!maps[i]->texture) continue;
      any=true;
      unsigned slot=0;
      while(slot<2&&slots[slot]&&!same_texture_binding(*slots[slot],*maps[i])) ++slot;
      if(slot==2) throw std::invalid_argument("njin renderer: clearcoat maps use at most two distinct texture bindings");
      if(!slots[slot]) slots[slot]=maps[i];
      select[i]=float(slot+1);
    }
    select[3]=draw.maps.clearcoat_normal_scale;
  }
};
inline bool has_material_maps(const MeshDraw &draw) {
  for(auto map:material_maps(draw)) if(map->texture) return true;
  return draw.maps.constant_emission || draw.color_uv.set || draw.color_uv.values!=std::array<float,6>{1,0,0,1,0,0};
}
struct MaterialUniforms {
  std::array<std::array<float,4>,10> uv{};
  std::array<float,4> factors{},emission{};
  explicit MaterialUniforms(const MeshDraw &draw) {
    const auto maps=material_maps(draw);
    for(unsigned i=0;i<5;++i) {
      const auto &t=i==4?draw.color_uv:maps[i]->uv; const auto &a=t.values;
      uv[i*2]={a[0],a[1],a[2],a[3]};
      uv[i*2+1]={a[4],a[5],float(t.set),i==4?1.0f:(maps[i]->texture?1.0f:0.0f)};
    }
    if(draw.maps.constant_emission && !draw.maps.emission.texture) uv[7][3]=2;
    const auto &m=draw.world;
    const float det=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
    factors={draw.maps.normal_scale,draw.maps.occlusion_strength,det<0?-1.0f:1.0f,0};
    emission=draw.maps.emission_factor;
  }
};
}
