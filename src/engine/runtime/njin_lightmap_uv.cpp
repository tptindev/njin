#include "njin_lightmap_internal.h"
#include <xatlas.h>
#include <raymath.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <exception>
#include <numeric>
#include <stdexcept>

namespace njin {
LightmapScene lightmap_prepare_uv(const LightmapScene &source,const LightmapSettings &settings,const std::function<bool(float)> &progress) {
  LightmapScene out=source;out.instances.clear();
  std::set<uint64_t> identities;
  unsigned receiver=0,total=0;for(const auto &instance:source.instances) total+=instance.receiver;
  for(const auto &instance:source.instances) {
    if(!instance.receiver) {out.instances.push_back(instance);continue;}
    if(instance.dynamic||!instance.geometry) throw std::runtime_error("njin lightmap unwrap: invalid/dynamic geometry");
    const auto &g=*instance.geometry;
    if(g.vertices.empty()||g.vertices.size()>65535) throw std::runtime_error("njin lightmap unwrap: mesh requires splitting below 65536 vertices");
    std::unique_ptr<xatlas::Atlas,void(*)(xatlas::Atlas*)> atlas(xatlas::Create(),xatlas::Destroy);
    struct Progress {
      const std::function<bool(float)> &fn;std::mutex mutex;bool cancelled=false;std::exception_ptr exception;
      unsigned receiver,total;float reported=0;
      Progress(const std::function<bool(float)> &callback,unsigned index,unsigned count):fn(callback),receiver(index),total(count) {}
    } state(progress,receiver++,total);
    xatlas::SetProgressCallback(atlas.get(),[](xatlas::ProgressCategory category,int percent,void *data) {
      auto &s=*static_cast<Progress*>(data);std::lock_guard lock(s.mutex);
      s.reported=std::max(s.reported,(float(unsigned(category))+std::clamp(percent,0,100)/100.f)/4);
      try {if(!s.cancelled&&s.fn&&!s.fn((s.receiver+s.reported)/s.total)) s.cancelled=true;}
      catch(...) {s.cancelled=true;s.exception=std::current_exception();}
      return !s.cancelled;
    },&state);
    std::vector<Vector3> positions;positions.reserve(g.vertices.size());
    const auto &m=instance.placement.transform;
    const Matrix world{m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14],m[3],m[7],m[11],m[15]};
    for(auto &v:g.vertices) positions.push_back(Vector3Transform({v.x,v.y,v.z},world));
    xatlas::MeshDecl input;input.vertexCount=unsigned(g.vertices.size());input.vertexPositionData=positions.data();input.vertexPositionStride=sizeof(Vector3);
    input.vertexUvData=&g.vertices[0].u;input.vertexUvStride=sizeof(RenderVertex);
    input.indexCount=unsigned(g.indices.size());input.indexData=g.indices.empty()?nullptr:g.indices.data();
    auto added=xatlas::AddMeshError::Error;
    std::vector<uint32_t> uv_materials;
    if(settings.generate_uv) added=xatlas::AddMesh(atlas.get(),input);
    else {
      const unsigned faces=unsigned((g.indices.empty()?g.vertices.size():g.indices.size())/3);
      uv_materials.resize(faces);std::iota(uv_materials.begin(),uv_materials.end(),0);
      const auto root=[&](unsigned face){while(uv_materials[face]!=face) face=uv_materials[face];return face;};
      std::map<std::pair<unsigned,unsigned>,unsigned> edges;
      for(unsigned face=0;face<faces;++face) for(unsigned corner=0;corner<3;++corner) {
        const auto index=[&](unsigned c){return g.indices.empty()?face*3+c:unsigned(g.indices[face*3+c]);};
        const unsigned a=index(corner),b=index((corner+1)%3);const auto key=std::minmax(a,b);
        auto [it,inserted]=edges.emplace(key,face);if(!inserted) uv_materials[root(face)]=root(it->second);
      }
      for(unsigned face=0;face<faces;++face) uv_materials[face]=root(face);
      xatlas::UvMeshDecl uv;uv.vertexCount=input.vertexCount;uv.vertexStride=sizeof(RenderVertex);uv.vertexUvData=g.vertices[0].lightmap_uv.data();uv.indexCount=input.indexCount;uv.indexData=input.indexData;
      uv.faceMaterialData=uv_materials.data();
      added=xatlas::AddUvMesh(atlas.get(),uv);
    }
    if(added!=xatlas::AddMeshError::Success) throw std::runtime_error("njin lightmap unwrap: xatlas rejected mesh/UV");
    xatlas::PackOptions pack;pack.resolution=settings.size;pack.padding=settings.padding;
    pack.texelsPerUnit=settings.generate_uv?settings.texels_per_unit:0;pack.bruteForce=true;pack.bilinear=true;
    xatlas::Generate(atlas.get(),{},pack); // ComputeCharts also partitions supplied UvMeshDecl charts.
    if(state.exception) std::rethrow_exception(state.exception);
    if(state.cancelled) throw std::runtime_error("njin lightmap unwrap: cancelled");
    if(atlas->meshCount!=1||!atlas->chartCount) throw std::runtime_error("njin lightmap unwrap: no chart generated");
    const auto &mesh=atlas->meshes[0];
    for(unsigned chart=0;chart<mesh.chartCount;++chart) {
      const auto &c=mesh.chartArray[chart];std::map<unsigned,unsigned> remap;
      auto geometry=std::make_shared<RenderGeometryData>();
      float low[2]{1e30f,1e30f},high[2]{-1e30f,-1e30f};
      for(unsigned f=0;f<c.faceCount;++f) for(unsigned corner=0;corner<3;++corner) {
        unsigned index=mesh.indexArray[c.faceArray[f]*3+corner];
        if(mesh.vertexArray[index].xref>=g.vertices.size()) throw std::runtime_error("njin lightmap unwrap: invalid remap");
        auto [it,inserted]=remap.emplace(index,unsigned(remap.size()));
        if(inserted) {
          if(geometry->vertices.size()>=65535) throw std::runtime_error("njin lightmap unwrap: split source mesh; chart exceeds 16-bit vertex limit");
          const auto &v=mesh.vertexArray[index];auto copy=g.vertices[v.xref];copy.lightmap_uv={v.uv[0],v.uv[1]};geometry->vertices.push_back(copy);
          for(unsigned axis=0;axis<2;++axis) {low[axis]=std::min(low[axis],v.uv[axis]);high[axis]=std::max(high[axis],v.uv[axis]);}
        }
        geometry->indices.push_back(uint16_t(it->second));
      }
      LightmapInstance target=instance;target.source_identity=instance.source_identity?instance.source_identity:instance.identity;
      target.source_geometry_fingerprint=lightmap_geometry_hash(g);
      // Stable chart identity; original object+mesh mapping remains explicit.
      target.identity=mesh.chartCount==1?instance.identity:instance.identity^(0x9e3779b97f4a7c15ull*(chart+1));if(!target.identity) target.identity=~instance.identity;
      while(!target.identity||identities.contains(target.identity)) ++target.identity;
      identities.insert(target.identity);
      float desired=1;
      if(settings.texels_per_unit>0) {
        double area_world=0,area_uv=0;
        for(size_t triangle=0;triangle<geometry->indices.size();triangle+=3) {
          Vector3 p[3];std::array<float,2> uv[3];
          for(unsigned corner=0;corner<3;++corner) {const auto &v=geometry->vertices[geometry->indices[triangle+corner]];p[corner]=Vector3Transform({v.x,v.y,v.z},world);uv[corner]=v.lightmap_uv;}
          area_world+=Vector3Length(Vector3CrossProduct(Vector3Subtract(p[1],p[0]),Vector3Subtract(p[2],p[0])))*.5;
          area_uv+=std::abs((uv[1][0]-uv[0][0])*(uv[2][1]-uv[0][1])-(uv[1][1]-uv[0][1])*(uv[2][0]-uv[0][0]))*.5;
        }
        if(area_uv<=0||area_world<=0) throw std::runtime_error("njin lightmap unwrap: zero chart area");
        desired=float(settings.texels_per_unit*std::sqrt(area_world/area_uv));
      }
      const float wanted_width=std::ceil((high[0]-low[0])*desired)+2*settings.padding+1,wanted_height=std::ceil((high[1]-low[1])*desired)+2*settings.padding+1;
      if(!std::isfinite(wanted_width)||!std::isfinite(wanted_height)) throw std::runtime_error("njin lightmap unwrap: nonfinite texel density/area");
      target.page_width=unsigned(std::clamp(wanted_width,8.f,float(instance.page_width?instance.page_width:settings.size)));
      target.page_height=unsigned(std::clamp(wanted_height,8.f,float(instance.page_height?instance.page_height:settings.size)));
      const float scale=std::min({desired,(target.page_width-2.f*settings.padding)/(high[0]-low[0]),(target.page_height-2.f*settings.padding)/(high[1]-low[1])});
      for(auto &v:geometry->vertices) {
        v.lightmap_uv={(settings.padding+(v.lightmap_uv[0]-low[0])*scale)/target.page_width,
                       (settings.padding+(v.lightmap_uv[1]-low[1])*scale)/target.page_height};
      }
      target.geometry=geometry;out.instances.push_back(std::move(target));
    }
  }
  return out;
}
}
