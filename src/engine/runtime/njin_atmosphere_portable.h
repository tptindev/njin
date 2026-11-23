#pragma once
#include "njin_internal_only.h"
#include "njin_atmosphere.h"
#include "njin_renderer.h"
#include <array>
#include <chrono>
namespace njin {
class CpuAtmosphere;
class AtmosphereJob;
struct AtmosphereState {
  RenderShaderHandle program;
  RenderTargetHandle transmittance,sky_view,multiple,aerial_radiance,aerial_transmittance;
  RenderTargetHandle environment_base,environment_atlas,environment_sky,aerial_scene;
  std::vector<RenderTargetHandle> retired_scenes;
  unsigned scene_width=0,scene_height=0;
  std::array<std::array<float,4>,18> previous{},current{},environment_key{},job_rows{},cpu_key{};
  std::shared_ptr<RenderEnvironmentData> environment;
  std::shared_ptr<CpuAtmosphere> cpu;
  std::shared_ptr<AtmosphereJob> job;
  std::chrono::steady_clock::time_point last_environment{};
  bool force_environment=false,view_environment=false,view_aerial=false;
  bool multiple_valid=false,aerial_valid=false;
  bool valid=false;
  atmosphere3d_stats stats;
};
struct model_store;
RenderTextureHandle portable_atmosphere_aerial(context &,model_store &,RenderTextureHandle);
std::array<std::array<float,4>,9> atmosphere_cpu_sh(AtmosphereState &,bool);
void atmosphere_cpu_begin(AtmosphereState &,const std::array<std::array<float,4>,18> &,bool);
bool atmosphere_cpu_ready(const AtmosphereState &);
std::array<std::array<float,4>,9> atmosphere_cpu_finish(AtmosphereState &);
}
