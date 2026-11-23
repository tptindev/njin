#pragma once
#include <njin_lightmap.h>

namespace njin::lightmap_fixture {
inline std::shared_ptr<RenderGeometryData> quad(float radius=1) {
  auto g=std::make_shared<RenderGeometryData>();
  g->vertices={{-radius,-radius,0,0,0},{radius,-radius,0,1,0},{radius,radius,0,1,1},{-radius,radius,0,0,1}};
  g->indices={0,1,2,0,2,3};
  for(auto &v:g->vertices) {v.lightmap_uv={.125f+.75f*v.u,.125f+.75f*v.v};v.uv1={1-v.u,v.v};}
  return g;
}
inline LightmapScene scene(bool blocker=true) {
  LightmapScene s;s.identity="njin-public-direct-fixture-v1";s.direction={0,0,-1};s.illuminance={6,3,1.5f};
  auto g=quad();s.instances.push_back({1,g});s.instances.push_back({2,g});
  s.instances[1].placement.transform[12]=3;
  if(blocker) {s.instances.push_back({3,quad(.35f)});s.instances.back().receiver=false;s.instances.back().placement.transform[14]=.5f;}
  return s;
}
inline std::shared_ptr<RenderGeometryData> cube() {
  auto g=std::make_shared<RenderGeometryData>();
  const std::array<float,3> normals[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  const std::array<float,3> axes[]{{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}};
  const std::array<float,3> ups[]{{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}};
  const float coords[][2]{{0,0},{1,0},{1,1},{0,1}};
  for(unsigned face=0;face<6;++face) {
    const uint16_t base=uint16_t(g->vertices.size());
    for(auto &uv:coords) {
      const auto &n=normals[face],&u=axes[face],&v=ups[face];
      RenderVertex vertex{n[0]*.5f+u[0]*(uv[0]-.5f)+v[0]*(uv[1]-.5f),n[1]*.5f+u[1]*(uv[0]-.5f)+v[1]*(uv[1]-.5f),n[2]*.5f+u[2]*(uv[0]-.5f)+v[2]*(uv[1]-.5f),uv[0],uv[1]};
      vertex.normal=n;vertex.uv1={1-uv[0],uv[1]};g->vertices.push_back(vertex);
    }
    for(auto index:{0,1,2,0,2,3}) g->indices.push_back(uint16_t(base+index));
  }
  return g;
}
}
