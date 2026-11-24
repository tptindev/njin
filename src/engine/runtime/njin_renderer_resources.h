#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
#include "_math.h"
#include "njin_rl.h"
#include <unordered_map>
#include <unordered_set>
namespace njin::rhi {
struct RendererResources {
  struct Draw2DState {
    RenderTextureHandle white{};
    RenderSpriteEffects sprite_effects;
    float alpha=1,scale=1;
    vec2 offset{};
    std::vector<float> alphas;
    std::vector<std::pair<vec2,float>> offsets;
    RenderBlend blend=RenderBlend::Alpha;
    bool clipped=false;
    RenderScissor clip{};
  };
  mutable Draw2DState draw_2d; // legacy drawing methods are logically const
  struct Texture { RenderImageAsset cpu; Texture2D gl{}; RenderTargetHandle target{}; bool alpha=false; std::array<float,8> virtual_params{}; };
  std::unordered_map<std::uint64_t,Texture> textures;
  struct MeshResource { std::vector<RenderVertex> cpu; std::vector<std::uint16_t> indices; Mesh gl{}; };
  std::unordered_map<std::uint64_t,MeshResource> meshes;
  struct Target { unsigned width=0,height=0; RenderTextureHandle color{}; RenderTexture2D gl{}; bool initialized=false; RenderTextureHandle depth{}; };
  std::unordered_map<std::uint64_t,Target> targets;
  RenderTargetHandle active_target{};
  bool world3d_open=false;
  std::unordered_set<std::uint64_t> pending_3d_textures,pending_3d_meshes;
  std::unordered_map<const void*,std::weak_ptr<const std::vector<RenderInstance>>> validated_instances;
  std::uint64_t instance_validation_frame=0;
  std::uint64_t texture_uploads=0;
  std::uint64_t mesh_uploads=0;
  std::uint64_t ray_traces=0,ray_bvh_builds=0;
  const void *ray_validated=nullptr; // scene data already checked by renderer_trace_shadows
  ~RendererResources(); // frees the legacy OpenGL objects (njin_renderer.cpp)
};
}
