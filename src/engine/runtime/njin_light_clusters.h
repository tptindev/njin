#pragma once
#include "njin_internal_only.h"
#include "njin_3d.h"
#include "njin_renderer.h"
#include <span>
#include <vector>
namespace njin {
// CPU cluster construction shared by legacy OpenGL and SDL3/bgfx.
struct LightClusters {
  i32 tiles_x=16,tiles_y=9,slices=24;
  f32 tile_px=1;
  vec4 depth{};
  std::vector<f32> cluster_data,index_data;
  std::vector<i32> counts,offsets;
};
void light_clusters_build(LightClusters &,std::span<const light3d_source>,const camera3d &,vec2 size);
struct context;struct model_store;
std::shared_ptr<const RenderClusterData> portable_light_clusters(context &,model_store &,const std::shared_ptr<const RenderShadowData> &);
}
