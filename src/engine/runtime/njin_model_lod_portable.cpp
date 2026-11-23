#include "njin_model_portable.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_log.h"
#include <meshoptimizer.h>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace njin {
namespace {
using Levels=std::vector<std::vector<RenderMeshHandle>>;
void available(context &ctx,const Levels &levels) {
  auto &window=ctx_ref<window_guard>(ctx);
  for(const auto &level:levels) for(auto mesh:level) if(mesh&&
      (window.renderer_resources.pending_3d_meshes.contains(mesh.id)||window.graphics->mesh_in_use(mesh)))
    throw std::logic_error("njin LOD: replace levels before submitting draws for this model");
}
void destroy(context &ctx,const Levels &levels) {
  for(const auto &level:levels) for(auto mesh:level) if(mesh) renderer_destroy_mesh(ctx,mesh);
}
void validate(float screen) {
  if(!std::isfinite(screen)||screen<0) throw std::invalid_argument("njin LOD: screen threshold must be finite and nonnegative");
}
// Keep original vertex indices. Skin weights, UV sets, tangents and morph
// streams stay aligned, and every LOD can use the existing deformation path.
std::vector<unsigned> welded(const RenderGeometryData &geometry,const PortableAnimation::Mesh *animation) {
  std::unordered_map<std::string,unsigned> unique;
  std::vector<unsigned> remap;remap.reserve(geometry.vertices.size());
  const size_t streams=animation?animation->dpos.size()*3:0;
  if(geometry.vertices.size()*(sizeof(RenderVertex)+streams*sizeof(Vector3)+20)>128u*1024u*1024u)
    throw std::runtime_error("njin LOD: morph welding budget exceeded");
  for(unsigned i=0;i<geometry.vertices.size();++i) {
    const auto &v=geometry.vertices[i];std::string key(reinterpret_cast<const char*>(&v),sizeof(v));
    if(animation) {
      const auto append=[&](const auto &value) {key.append(reinterpret_cast<const char*>(&value),sizeof(value));};
      if(!animation->bone.empty()) {append(animation->bone[i]);append(animation->weights[i]);}
      for(size_t k=0;k<animation->dpos.size();++k) {
        append(animation->dpos[k][i]);append(animation->dnormal[k][i]);append(animation->dtangent[k][i]);
      }
    }
    remap.push_back(unique.emplace(std::move(key),i).first->second);
  }
  std::vector<unsigned> indices;
  if(geometry.indices.empty()) {indices=remap;}
  else {indices.reserve(geometry.indices.size());for(auto i:geometry.indices) indices.push_back(remap[i]);}
  return indices;
}
}
void portable_model_clear_lods(model_store &store,model_slot &slot) {
  auto &ctx=*store.portable_context;available(ctx,slot.portable_lods);
  destroy(ctx,slot.portable_lods);slot.portable_lods.clear();
}
std::vector<RenderGeometryData> portable_lod_prepare(const RenderGeometryData &geometry,const PortableAnimation::Mesh *animation,const model_lod_desc &desc) {
  validate(desc.screen);
  if(!std::isfinite(desc.ratio)||!std::isfinite(desc.max_error)||desc.max_error<0)
    throw std::invalid_argument("njin streaming LOD: invalid ratio/error");
  std::vector<RenderGeometryData> levels;
  auto indices=welded(geometry,animation);
  if(geometry.vertices.size()>65535||indices.size()<24) return levels;
  std::vector<RenderVertex> compact_source;
  if(!animation) {
    compact_source.resize(geometry.vertices.size());
    const auto kept=meshopt_optimizeVertexFetch(compact_source.data(),indices.data(),indices.size(),geometry.vertices.data(),geometry.vertices.size(),sizeof(RenderVertex));
    compact_source.resize(kept);
  }
  const auto &source=animation?geometry.vertices:compact_source;
  const unsigned count=unsigned(std::clamp(desc.levels,1,4));const float ratio=std::clamp(desc.ratio,.1f,.9f);
  const size_t morphs=animation?std::min<size_t>(10,animation->dpos.size()):0,components=2+morphs*3;
  std::vector<float> attributes(source.size()*components),weights(components,1.f);weights[0]=weights[1]=.1f;
  for(size_t v=0;v<source.size();++v) {
    attributes[v*components]=source[v].u;attributes[v*components+1]=source[v].v;
    for(size_t m=0;m<morphs;++m) {const auto p=animation->dpos[m][v];attributes[v*components+2+m*3]=p.x;attributes[v*components+3+m*3]=p.y;attributes[v*components+4+m*3]=p.z;}
  }
  std::vector<unsigned> simplified(indices.size());size_t last=indices.size();
  for(unsigned level=0;level<count;++level) {
    const auto target=std::max<size_t>(3,size_t(float(indices.size())*std::pow(ratio,float(level+1)))/3*3);
    const auto kept=meshopt_simplifyWithAttributes(simplified.data(),indices.data(),indices.size(),&source.front().x,
      source.size(),sizeof(RenderVertex),attributes.data(),components*sizeof(float),weights.data(),components,nullptr,target,desc.max_error,0,nullptr);
    if(kept<3||float(kept)>float(last)*.9f) break;
    RenderGeometryData out;out.vertices=source;
    if(!animation) {
      std::vector<unsigned> compact(simplified.begin(),simplified.begin()+kept);
      const auto vertices=meshopt_optimizeVertexFetch(out.vertices.data(),compact.data(),compact.size(),source.data(),source.size(),sizeof(RenderVertex));
      out.vertices.resize(vertices);for(auto i:compact) out.indices.push_back(u16(i));
    } else for(size_t i=0;i<kept;++i) out.indices.push_back(u16(simplified[i]));
    levels.push_back(std::move(out));last=kept;
  }
  return levels;
}
RenderMeshHandle portable_model_lod_mesh(const model_slot &slot,unsigned mesh,unsigned level) {
  if(slot.streaming_lods) {
    level=std::min(level,unsigned(slot.portable_lods.size()));
    for(unsigned k=level;k<=slot.portable_lods.size();++k) {
      auto h=k?slot.portable_lods[k-1].at(mesh):slot.portable_meshes.at(mesh);if(h) return h;
    }
    for(unsigned k=level;k>0;--k) {auto h=k==1?slot.portable_meshes.at(mesh):slot.portable_lods[k-2].at(mesh);if(h) return h;}
    throw std::logic_error("njin streaming LOD: no resident fallback");
  }
  level=std::min(level,unsigned(slot.portable_lods.size()));
  while(level) {const auto handle=slot.portable_lods[--level].at(mesh);if(handle) return handle;}
  return slot.portable_meshes.at(mesh);
}
int portable_model_lod_build(context &ctx,model_handle handle,const model_lod_desc &desc) {
  auto &store=ctx_ref<model_store>(ctx);auto *slot=model_slot_of(store,handle);if(!slot) return 0;
  if(slot->streaming_lods) throw std::logic_error("njin LOD: use streaming_detail for a streamed LOD model");
  validate(desc.screen);
  if(!std::isfinite(desc.ratio)||!std::isfinite(desc.max_error)||desc.max_error<0)
    throw std::invalid_argument("njin LOD: invalid simplification ratio/error");
  available(ctx,slot->portable_lods);
  const unsigned count=unsigned(std::clamp(desc.levels,1,4));const float ratio=std::clamp(desc.ratio,.1f,.9f);
  Levels levels(count,std::vector<RenderMeshHandle>(slot->portable_meshes.size()));unsigned built=0;
  try {
    for(unsigned mesh=0;mesh<slot->portable_meshes.size();++mesh) {
      const auto &source=ctx_ref<window_guard>(ctx).renderer_resources.meshes.at(slot->portable_meshes[mesh].id);
      if(source.cpu.size()>65535||source.cpu.size()<3) continue;
      const RenderGeometryData geometry{source.cpu,source.indices};
      const auto *animation=slot->portable_animation?&slot->portable_animation->meshes.at(mesh):nullptr;
      const auto indices=welded(geometry,animation);if(indices.size()<24) continue;
      std::vector<unsigned> simplified(indices.size());size_t last=indices.size();
      const size_t morphs=animation?std::min<size_t>(10,animation->dpos.size()):0,attributes_count=2+morphs*3;
      std::vector<float> attributes(source.cpu.size()*attributes_count),weights(attributes_count,1.f);weights[0]=weights[1]=.1f;
      for(size_t v=0;v<source.cpu.size();++v) {
        attributes[v*attributes_count]=source.cpu[v].u;attributes[v*attributes_count+1]=source.cpu[v].v;
        for(size_t k=0;k<morphs;++k) {const auto p=animation->dpos[k][v];attributes[v*attributes_count+2+k*3]=p.x;attributes[v*attributes_count+3+k*3]=p.y;attributes[v*attributes_count+4+k*3]=p.z;}
      }
      for(unsigned level=0;level<count;++level) {
        const auto target=std::max<size_t>(3,size_t(float(indices.size())*std::pow(ratio,float(level+1)))/3*3);
        const auto kept=meshopt_simplifyWithAttributes(simplified.data(),indices.data(),indices.size(),
          &source.cpu.front().x,source.cpu.size(),sizeof(RenderVertex),attributes.data(),attributes_count*sizeof(float),
          weights.data(),attributes_count,nullptr,target,desc.max_error,0,nullptr);
        if(kept<3||float(kept)>float(last)*.9f) continue;
        std::vector<std::uint16_t> compact;compact.reserve(kept);for(size_t i=0;i<kept;++i) compact.push_back(std::uint16_t(simplified[i]));
        levels[level][mesh]=renderer_create_mesh(ctx,source.cpu,compact);last=kept;built=std::max(built,level+1);
      }
    }
    portable_model_clear_lods(store,*slot);levels.resize(built);slot->portable_lods=std::move(levels);slot->lod_screen=desc.screen;
  } catch(...) {destroy(ctx,levels);throw;}
  return int(built);
}
int portable_model_lod_load(context &ctx,model_handle handle,const char *const *paths,int count,float screen) {
  auto &store=ctx_ref<model_store>(ctx);const auto *base=model_slot_of(store,handle);if(!base) return 0;
  if(base->streaming_lods) throw std::logic_error("njin LOD: use streaming_detail for a streamed LOD model");
  validate(screen);
  if(ctx_ref<window_guard>(ctx).graphics->frame_open()) throw std::logic_error("njin LOD: load levels outside render frames");
  if(base->portable_animation) throw std::invalid_argument("njin LOD: file levels require a static base model");
  if(!paths||count<1||count>4) return 0;
  available(ctx,base->portable_lods);const auto meshes=base->portable_meshes.size();const bool merge=base->merged;Levels levels;
  try {
    for(int i=0;i<count;++i) {
      const auto temporary=portable_model_load(store,{.path=paths[i],.merge=merge});
      auto *slot=model_slot_of(store,temporary);
      if(!slot||slot->portable_animation||slot->portable_meshes.size()!=meshes) {
        if(slot) portable_model_unload(store,temporary);
        destroy(ctx,levels);NJIN_WARN("njin LOD: file mesh layout does not match the base model");return 0;
      }
      try {levels.push_back(slot->portable_meshes);} catch(...) {portable_model_unload(store,temporary);throw;}
      slot->portable_meshes.clear();portable_model_unload(store,temporary);
    }
    auto &slot=*model_slot_of(store,handle);portable_model_clear_lods(store,slot);slot.portable_lods=std::move(levels);slot.lod_screen=screen;
  } catch(...) {destroy(ctx,levels);throw;}
  return count;
}
}
