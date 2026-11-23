#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
#include "njin_3d.h"
#include <raylib.h>
#include <raymath.h>
#include <unordered_map>
namespace njin {
struct context;struct model_store;
struct PortableMotionRecord {
  Matrix world{};
  std::shared_ptr<const RenderGeometryData> geometry;
};
struct PortableTemporalState {
  std::uint64_t frame=0,last_frame=0,next_id=0,next_namespace=0;
  bool active=false,first=false,valid=false,taa_valid=false;
  unsigned jitter_index=0,history_index=0,width=0,height=0;
  double time=0;
  float exposure=1;
  vec2 jitter{},previous_jitter{};
  camera3d previous_camera;
  Matrix previous_view{},previous_vp{},previous_jittered_vp{},current_vp{};
  std::unordered_map<std::uint64_t,unsigned> occurrences;
  std::unordered_map<std::uint64_t,PortableMotionRecord> previous,current;
  RenderTargetHandle velocity,blur;
  std::array<RenderTargetHandle,2> history{},depth{};
};
void portable_temporal_begin(context &,model_store &,Matrix &projection,const Matrix &view,unsigned width,unsigned height);
std::uint64_t portable_temporal_key(model_store &,std::uint64_t model);
void portable_temporal_record(model_store &,MeshDraw &,std::uint64_t key,const Matrix &world);
void portable_temporal_velocity(context &,model_store &);
RenderTextureHandle portable_temporal_resolve(context &,model_store &,RenderTextureHandle);
RenderTextureHandle portable_motion_blur(context &,model_store &,RenderTextureHandle);
void portable_temporal_finish(model_store &);
void portable_temporal_free(context &,model_store &);
}
