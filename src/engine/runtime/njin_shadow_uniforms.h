#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
namespace njin::rhi {
struct ShadowUniforms {
  std::array<std::array<float,4>,32> rows{};
  std::array<std::array<float,4>,28> regions{}, metadata{};
  std::array<float,4> info{}, splits{}, forward{};
  explicit ShadowUniforms(const MeshDraw &draw) {
    if(draw.shadows && draw.receive_shadow) {
      const auto &s=*draw.shadows;
      rows=s.matrices; regions=s.regions; metadata=s.metadata;
      info={1.0f/s.width,1.0f/s.height,s.softness,float(s.views)};
      splits=s.splits; forward=s.forward;
    }
    if(draw.shadow_pass) info[3]=-1;
  }
};
}
