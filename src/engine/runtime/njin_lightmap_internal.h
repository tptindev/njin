#pragma once
#include "njin_internal_only.h"
#include "njin_lightmap.h"
#include <unordered_map>
namespace njin {
struct LightmapResource {
  LightmapAsset asset;
  std::unordered_map<std::uint64_t,std::vector<LightmapBinding>> objects;
};
std::span<const LightmapBinding> lightmap_object_bindings(const context &,lightmap_handle,std::uint64_t);
std::uint64_t lightmap_geometry_hash(const RenderGeometryData &);
LightmapScene lightmap_prepare_uv(const LightmapScene &,const LightmapSettings &,const std::function<bool(float)> &);
}
