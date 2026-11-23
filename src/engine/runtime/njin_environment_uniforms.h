#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
namespace njin::rhi {
struct EnvironmentUniforms {
  std::array<std::array<float,4>,9> sh{};
  std::array<float,4> params{},mode{};
  explicit EnvironmentUniforms(const MeshDraw &draw) {
    if(draw.sky&&draw.sky->backdrop) {mode[0]=3;return;} // procedural sky quad (u_njinSky)
    if(!draw.environment) return;
    const auto &env=*draw.environment;sh=env.sh;
    for(auto &c:sh) for(unsigned k=0;k<3;++k) c[k]*=env.intensity;
    params={float(env.levels),env.intensity,env.rotation,.5f/env.level_height};
    mode[0]=draw.environment_sky?2.f:1.f;
  }
};
}
