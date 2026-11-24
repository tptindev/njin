#pragma once
#include "njin_internal_only.h"
#include "njin_3d.h"
#include "njin_renderer.h"
#include "njin_rl.h"
#include <memory>
#include <string>
#include <vector>
struct cgltf_data;
struct cgltf_node;
struct cgltf_primitive;
namespace njin {
struct model_slot;
struct PortablePrimitive {const cgltf_node *node;const cgltf_primitive *primitive;};
struct PortableAnimation {
  struct Node {int parent=-1;Transform rest{};Matrix fixed{};bool matrix=false;std::vector<int> morphs;std::vector<float> morph_weights;};
  struct Channel {int node=-1,path=0,interpolation=0;unsigned components=0;std::vector<float> times,values;};
  struct Clip {std::string name;float duration=0;std::vector<Channel> channels;};
  struct Mesh {
    int node=-1;
    Matrix inverse_node{};
    std::vector<int> joints;
    std::vector<Matrix> inverse_bind;
    std::vector<std::array<unsigned char,4>> bone;
    std::vector<std::array<float,4>> weights;
    std::vector<std::vector<Vector3>> dpos,dnormal,dtangent;
    std::vector<int> morphs;
    RenderGeometryData base;
  };
  std::vector<Node> nodes;
  std::vector<Clip> clips;
  std::shared_ptr<const PortableAnimation> animation_library;
  bool borrowed_clips=false;
  const std::vector<Clip> &clips_view() const {auto *library=this;while(library->animation_library) library=library->animation_library.get();return library->clips;}
  std::vector<Mesh> meshes;
  std::vector<int> joints;
  std::vector<BoneInfo> bones;
  std::vector<Transform> bind_pose;
  std::vector<std::string> morph_names;
  std::vector<float> morph_defaults;
};
std::shared_ptr<PortableAnimation> portable_animation_build(const cgltf_data &,std::span<const PortablePrimitive>);
void portable_animation_attach(model_slot &);
std::shared_ptr<const RenderGeometryData> portable_animation_geometry(const model_slot &,unsigned,const model_pose *,std::span<const std::uint16_t> indices={});
std::vector<Matrix> portable_animation_bones(const model_slot &,const model_pose &);
void portable_animation_morphs(const model_slot &,const model_pose *,std::vector<float> &);
}
