#include "njin_animation_portable.h"
#include "njin_model.h"
#include <external/cgltf.h>
#include "njin_rlmath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
namespace njin {
namespace {
constexpr size_t limit=10000000;
Matrix matrix(const float *v) {return {v[0],v[4],v[8],v[12],v[1],v[5],v[9],v[13],v[2],v[6],v[10],v[14],v[3],v[7],v[11],v[15]};}
Matrix matrix(const Transform &t) {return MatrixMultiply(MatrixMultiply(MatrixScale(t.scale.x,t.scale.y,t.scale.z),QuaternionToMatrix(t.rotation)),MatrixTranslate(t.translation.x,t.translation.y,t.translation.z));}
std::vector<float> unpack(const cgltf_accessor *a,size_t expected) {
  if(!a||!expected||expected>limit) throw std::runtime_error("njin animation: invalid accessor size");
  std::vector<float> out(expected);
  if(cgltf_accessor_unpack_floats(a,out.data(),expected)!=expected) throw std::runtime_error("njin animation: could not decode accessor");
  for(float v:out) if(!std::isfinite(v)) throw std::runtime_error("njin animation: non-finite animation data");
  return out;
}
Vector3 direction(Vector3 v,Matrix m) {m.m12=m.m13=m.m14=0;return Vector3Transform(v,m);}
std::vector<float> curve(const PortableAnimation::Channel &c,float time) {
  const auto n=c.times.size();const auto upper=std::upper_bound(c.times.begin(),c.times.end(),time);
  const size_t lo=upper==c.times.begin()?0:size_t(upper-c.times.begin()-1),hi=std::min(lo+1,n-1);
  const float duration=c.times[hi]-c.times[lo],t=duration>0?std::clamp((time-c.times[lo])/duration,0.f,1.f):0;
  const bool cubic=c.interpolation==int(cgltf_interpolation_type_cubic_spline);
  const auto at=[&](size_t key,unsigned component,unsigned part=1) {return c.values[(key*(cubic?3:1)+(cubic?part:0))*c.components+component];};
  std::vector<float> out(c.components);
  if(c.path==int(cgltf_animation_path_type_rotation)&&!cubic&&c.interpolation!=int(cgltf_interpolation_type_step)) {
    const auto q=QuaternionSlerp({at(lo,0),at(lo,1),at(lo,2),at(lo,3)},{at(hi,0),at(hi,1),at(hi,2),at(hi,3)},t);out={q.x,q.y,q.z,q.w};return out;
  }
  for(unsigned k=0;k<c.components;++k) {
    const float a=at(lo,k),b=at(hi,k);
    if(cubic&&hi!=lo) out[k]=(2*t*t*t-3*t*t+1)*a+(t*t*t-2*t*t+t)*duration*at(lo,k,2)+(-2*t*t*t+3*t*t)*b+(t*t*t-t*t)*duration*at(hi,k,0);
    else out[k]=c.interpolation==int(cgltf_interpolation_type_step)?a:a+(b-a)*t;
  }
  if(c.path==int(cgltf_animation_path_type_rotation)) {
    const auto q=QuaternionNormalize({out[0],out[1],out[2],out[3]});out={q.x,q.y,q.z,q.w};
  }
  return out;
}
struct Pose {std::vector<std::vector<float>> node_morphs;std::vector<Transform> local;std::vector<float> morphs;std::vector<Matrix> world;};
Pose sample(const PortableAnimation &a,int clip,float time,bool loop) {
  if(!std::isfinite(time)) throw std::invalid_argument("njin animation: invalid clip time");
  Pose p;for(auto &node:a.nodes) {p.local.push_back(node.rest);p.node_morphs.push_back(node.morph_weights);}p.morphs=a.morph_defaults;
  const auto &clips=a.clips_view();
  if(a.borrowed_clips&&clips.empty()) clip=-1; // A released library leaves the copied bind skeleton usable.
  if(clip>=0) {
    if(clip>=int(clips.size())) throw std::invalid_argument("njin animation: invalid clip index");
    const auto &c=clips[clip];
    if(loop&&c.duration>0) {time=std::fmod(time,c.duration);if(time<0) time+=c.duration;} else time=std::clamp(time,0.f,c.duration);
    for(const auto &channel:c.channels) {
      const auto values=curve(channel,time);auto &t=p.local[channel.node];
      if(channel.path==int(cgltf_animation_path_type_translation)) t.translation={values[0],values[1],values[2]};
      else if(channel.path==int(cgltf_animation_path_type_rotation)) t.rotation={values[0],values[1],values[2],values[3]};
      else if(channel.path==int(cgltf_animation_path_type_scale)) t.scale={values[0],values[1],values[2]};
      else for(unsigned k=0;k<values.size();++k) p.node_morphs[channel.node][k]=values[k];
    }
  }
  std::vector<bool> assigned(a.morph_names.size());
  for(size_t i=0;i<a.nodes.size();++i) for(size_t k=0;k<a.nodes[i].morphs.size();++k) {
    const auto id=a.nodes[i].morphs[k];if(!assigned[id]) {p.morphs[id]=p.node_morphs[i][k];assigned[id]=true;}
  }
  return p;
}
Pose pose(const PortableAnimation &a,const model_pose *requested) {
  const model_pose rest{};const auto &r=requested?*requested:rest;
  if(!std::isfinite(r.blend)||r.morph_count<0) throw std::invalid_argument("njin animation: invalid blend or morph count");
  auto p=sample(a,r.anim,r.time,r.loop);
  if(r.blend_anim>=0&&r.blend>0) {
    const auto other=sample(a,r.blend_anim,r.blend_time,r.blend_loop);const float k=std::clamp(r.blend,0.f,1.f);
    for(size_t i=0;i<p.local.size();++i) {p.local[i].translation=Vector3Lerp(p.local[i].translation,other.local[i].translation,k);p.local[i].rotation=QuaternionSlerp(p.local[i].rotation,other.local[i].rotation,k);p.local[i].scale=Vector3Lerp(p.local[i].scale,other.local[i].scale,k);}
    for(size_t i=0;i<p.node_morphs.size();++i) for(size_t j=0;j<p.node_morphs[i].size();++j) p.node_morphs[i][j]+=(other.node_morphs[i][j]-p.node_morphs[i][j])*k;
    for(size_t i=0;i<p.morphs.size();++i) p.morphs[i]+=(other.morphs[i]-p.morphs[i])*k;
  }
  if(r.morph_weights) for(int i=0;i<r.morph_count&&size_t(i)<p.morphs.size();++i) {
    if(!std::isfinite(r.morph_weights[i])) throw std::invalid_argument("njin animation: non-finite morph weight");p.morphs[i]+=r.morph_weights[i];
    for(size_t node=0;node<a.nodes.size();++node) for(size_t k=0;k<a.nodes[node].morphs.size();++k) if(a.nodes[node].morphs[k]==i) p.node_morphs[node][k]+=r.morph_weights[i];
  }
  p.world.resize(a.nodes.size());std::vector<unsigned char> state(a.nodes.size());
  const auto resolve=[&](auto &&self,size_t i)->Matrix {
    if(state[i]==2) return p.world[i];if(state[i]==1) throw std::runtime_error("njin animation: cyclic node hierarchy");state[i]=1;
    const auto &node=a.nodes[i];auto m=node.matrix?node.fixed:matrix(p.local[i]);if(node.parent>=0) m=MatrixMultiply(m,self(self,size_t(node.parent)));
    p.world[i]=m;state[i]=2;return m;
  };
  for(size_t i=0;i<a.nodes.size();++i) resolve(resolve,i);
  if(r.bones) for(size_t i=0;i<a.joints.size();++i) {
    const auto &b=r.bones[i];Matrix m=MatrixIdentity();m.m0=b.x_axis.x;m.m1=b.x_axis.y;m.m2=b.x_axis.z;m.m4=b.y_axis.x;m.m5=b.y_axis.y;m.m6=b.y_axis.z;m.m8=b.z_axis.x;m.m9=b.z_axis.y;m.m10=b.z_axis.z;m.m12=b.position.x;m.m13=b.position.y;m.m14=b.position.z;p.world[a.joints[i]]=m;
  }
  return p;
}
}
std::shared_ptr<PortableAnimation> portable_animation_build(const cgltf_data &raw,std::span<const PortablePrimitive> primitives) {
  bool morph=false;for(auto &p:primitives) morph|=p.primitive->targets_count!=0;
  if(!raw.skins_count&&!raw.animations_count&&!morph) return {};
  if(raw.nodes_count>4096||raw.animations_count>1024) throw std::runtime_error("njin animation: node/clip budget exceeded");
  size_t decoded=0;
  const auto read=[&](const cgltf_accessor *accessor,size_t count) {
    if(count>limit-decoded) throw std::runtime_error("njin animation: decoded animation budget exceeded");
    decoded+=count;return unpack(accessor,count);
  };
  auto a=std::make_shared<PortableAnimation>();a->nodes.resize(raw.nodes_count);
  std::unordered_map<const cgltf_node*,int> node_ids;for(size_t i=0;i<raw.nodes_count;++i) node_ids.emplace(raw.nodes+i,int(i));
  for(size_t i=0;i<raw.nodes_count;++i) {
    const auto &n=raw.nodes[i];auto &out=a->nodes[i];out.parent=n.parent?node_ids.at(n.parent):-1;
    out.rest={{n.translation[0],n.translation[1],n.translation[2]},{n.rotation[0],n.rotation[1],n.rotation[2],n.rotation[3]},{n.scale[0],n.scale[1],n.scale[2]}};
    const float components[]{n.translation[0],n.translation[1],n.translation[2],n.rotation[0],n.rotation[1],n.rotation[2],n.rotation[3],n.scale[0],n.scale[1],n.scale[2]};
    for(float v:components) if(!std::isfinite(v)) throw std::runtime_error("njin animation: invalid node transform");
    if(QuaternionLength(out.rest.rotation)<1e-8f) throw std::runtime_error("njin animation: zero node quaternion");
    out.rest.rotation=QuaternionNormalize(out.rest.rotation);
    out.matrix=n.has_matrix;out.fixed=matrix(n.matrix);
    if(out.matrix) {for(float v:n.matrix) if(!std::isfinite(v)) throw std::runtime_error("njin animation: non-finite node matrix");
      if(n.matrix[3]!=0||n.matrix[7]!=0||n.matrix[11]!=0||n.matrix[15]!=1) throw std::runtime_error("njin animation: node matrix must be affine");}
    if(n.mesh) {
      size_t targets=0;for(size_t k=0;k<n.mesh->primitives_count;++k) targets=std::max(targets,n.mesh->primitives[k].targets_count);
      if(targets>1024) throw std::runtime_error("njin animation: morph target budget exceeded");
      for(size_t k=0;k<targets;++k) {
        const std::string name=k<n.mesh->target_names_count?n.mesh->target_names[k]:std::string(n.mesh->name?n.mesh->name:"mesh")+"."+std::to_string(k);
        const float weight=k<n.weights_count?n.weights[k]:k<n.mesh->weights_count?n.mesh->weights[k]:0;
        if(!std::isfinite(weight)) throw std::runtime_error("njin animation: invalid default morph weight");
        const auto found=std::find(a->morph_names.begin(),a->morph_names.end(),name);int slot=int(found-a->morph_names.begin());
        if(found==a->morph_names.end()) {a->morph_names.push_back(name);a->morph_defaults.push_back(weight);}out.morphs.push_back(slot);out.morph_weights.push_back(weight);
      }
    }
  }
  for(size_t s=0;s<raw.skins_count;++s) for(size_t k=0;k<raw.skins[s].joints_count;++k) {
    const int node=node_ids.at(raw.skins[s].joints[k]);if(std::find(a->joints.begin(),a->joints.end(),node)==a->joints.end()) a->joints.push_back(node);
  }
  if(a->joints.size()>128) throw std::runtime_error("njin animation: more than 128 distinct skeleton joints");
  const auto rest=pose(*a,nullptr);
  for(int node:a->joints) {
    BoneInfo bone{};const auto &n=raw.nodes[node];std::snprintf(bone.name,sizeof(bone.name),"%s",n.name?n.name:"");bone.parent=-1;
    for(auto *parent=n.parent;parent;parent=parent->parent) {auto found=std::find(a->joints.begin(),a->joints.end(),node_ids.at(parent));if(found!=a->joints.end()) {bone.parent=int(found-a->joints.begin());break;}}
    a->bones.push_back(bone);Transform t;MatrixDecompose(rest.world[node],&t.translation,&t.rotation,&t.scale);a->bind_pose.push_back(t);
  }
  for(size_t i=0;i<raw.animations_count;++i) {
    const auto &input=raw.animations[i];PortableAnimation::Clip clip;clip.name=input.name?input.name:"";
    for(size_t k=0;k<input.channels_count;++k) {
      const auto &c=input.channels[k];if(!c.target_node||!c.sampler) throw std::runtime_error("njin animation: invalid channel target");
      PortableAnimation::Channel out;out.node=node_ids.at(c.target_node);out.path=int(c.target_path);out.interpolation=int(c.sampler->interpolation);
      if(c.target_path==cgltf_animation_path_type_weights) out.components=unsigned(a->nodes[out.node].morphs.size());
      else if(c.target_path==cgltf_animation_path_type_rotation) out.components=4;
      else if(c.target_path==cgltf_animation_path_type_translation||c.target_path==cgltf_animation_path_type_scale) out.components=3;
      else throw std::runtime_error("njin animation: unsupported channel path");
      if(c.target_path!=cgltf_animation_path_type_weights&&a->nodes[out.node].matrix) throw std::runtime_error("njin animation: TRS channel targets a matrix node");
      if(out.interpolation!=int(cgltf_interpolation_type_step)&&out.interpolation!=int(cgltf_interpolation_type_linear)&&out.interpolation!=int(cgltf_interpolation_type_cubic_spline)) throw std::runtime_error("njin animation: invalid interpolation");
      if(!c.sampler->input||!c.sampler->output||c.sampler->input->type!=cgltf_type_scalar) throw std::runtime_error("njin animation: invalid sampler");
      out.times=read(c.sampler->input,c.sampler->input->count);
      for(size_t t=0;t<out.times.size();++t) if(out.times[t]<0||(t&&out.times[t]<=out.times[t-1])) throw std::runtime_error("njin animation: key times must increase");
      const size_t values=out.times.size()*out.components*(out.interpolation==int(cgltf_interpolation_type_cubic_spline)?3:1);
      if(cgltf_num_components(c.sampler->output->type)*c.sampler->output->count!=values) throw std::runtime_error("njin animation: sampler output size mismatch");
      out.values=read(c.sampler->output,values);
      if(c.target_path==cgltf_animation_path_type_rotation) for(size_t key=0;key<out.times.size();++key) {
        const size_t offset=(key*(out.interpolation==int(cgltf_interpolation_type_cubic_spline)?3:1)+(out.interpolation==int(cgltf_interpolation_type_cubic_spline)?1:0))*4;
        float length=0;for(unsigned component=0;component<4;++component) length+=out.values[offset+component]*out.values[offset+component];
        if(std::abs(length-1)>0.01f) throw std::runtime_error("njin animation: rotation keys must be unit quaternions");
      }
      clip.duration=std::max(clip.duration,out.times.back());clip.channels.push_back(std::move(out));
    }
    a->clips.push_back(std::move(clip));
  }
  for(const auto &binding:primitives) {
    PortableAnimation::Mesh mesh;mesh.node=node_ids.at(binding.node);mesh.inverse_node=MatrixInvert(rest.world[mesh.node]);
    if(std::abs(MatrixDeterminant(rest.world[mesh.node]))<1e-12f) throw std::runtime_error("njin animation: singular rest node transform");
    const auto &p=*binding.primitive;const auto *positions=cgltf_find_accessor(&p,cgltf_attribute_type_position,0);const size_t count=positions->count;
    if(binding.node->skin) {
      const auto &skin=*binding.node->skin;std::vector<float> inverse;
      if(skin.inverse_bind_matrices) {if(skin.inverse_bind_matrices->count!=skin.joints_count||skin.inverse_bind_matrices->type!=cgltf_type_mat4) throw std::runtime_error("njin animation: invalid inverse bind matrices");inverse=read(skin.inverse_bind_matrices,skin.joints_count*16);}
      for(size_t k=0;k<skin.joints_count;++k) {
        mesh.joints.push_back(node_ids.at(skin.joints[k]));mesh.inverse_bind.push_back(inverse.empty()?MatrixIdentity():matrix(inverse.data()+k*16));
      }
      const auto *j=cgltf_find_accessor(&p,cgltf_attribute_type_joints,0),*w=cgltf_find_accessor(&p,cgltf_attribute_type_weights,0);
      if(!j||!w||j->count!=count||w->count!=count||j->type!=cgltf_type_vec4||w->type!=cgltf_type_vec4||cgltf_find_accessor(&p,cgltf_attribute_type_joints,1)) throw std::runtime_error("njin animation: expected four joint influences per vertex");
      const auto joints=read(j,count*4),weights=read(w,count*4);mesh.bone.resize(count);mesh.weights.resize(count);
      for(size_t v=0;v<count;++v) {
        float sum=0;for(unsigned k=0;k<4;++k) {const float id=joints[v*4+k],weight=weights[v*4+k];if(id<0||id>=mesh.joints.size()||id!=std::floor(id)||weight<0) throw std::runtime_error("njin animation: invalid skin influence");mesh.bone[v][k]=static_cast<unsigned char>(id);sum+=weight;}
        if(sum<1e-8f) throw std::runtime_error("njin animation: skin weights sum to zero");for(unsigned k=0;k<4;++k) mesh.weights[v][k]=weights[v*4+k]/sum;
      }
    }
    for(size_t k=0;k<p.targets_count;++k) {
      mesh.morphs.push_back(a->nodes[mesh.node].morphs.at(k));
      const auto delta=[&](cgltf_attribute_type type,Matrix transform) {
        const cgltf_accessor *accessor=nullptr;for(size_t i=0;i<p.targets[k].attributes_count;++i) if(p.targets[k].attributes[i].type==type) accessor=p.targets[k].attributes[i].data;
        if(!accessor) {
          if(count*3>limit-decoded) throw std::runtime_error("njin animation: morph delta budget exceeded");decoded+=count*3;
          return std::vector<Vector3>(count);
        }
        std::vector<Vector3> out(count);
        if(accessor->count!=count||accessor->type!=cgltf_type_vec3) throw std::runtime_error("njin animation: invalid morph delta");const auto values=read(accessor,count*3);
        for(size_t v=0;v<count;++v) out[v]=direction({values[v*3],values[v*3+1],values[v*3+2]},transform);return out;
      };
      mesh.dpos.push_back(delta(cgltf_attribute_type_position,rest.world[mesh.node]));
      auto normal_delta=delta(cgltf_attribute_type_normal,MatrixTranspose(mesh.inverse_node));
      const auto *normal=cgltf_find_accessor(&p,cgltf_attribute_type_normal,0);
      if(normal) {
        if(normal->count!=count||normal->type!=cgltf_type_vec3) throw std::runtime_error("njin animation: invalid base normal");
        const auto normals=read(normal,count*3);
        for(size_t v=0;v<count;++v) {
          const auto baked=direction({normals[v*3],normals[v*3+1],normals[v*3+2]},MatrixTranspose(mesh.inverse_node));
          const float length=Vector3Length(baked);if(length>1e-8f) normal_delta[v]=Vector3Scale(normal_delta[v],1/length);
        }
      }
      mesh.dnormal.push_back(std::move(normal_delta));mesh.dtangent.push_back(delta(cgltf_attribute_type_tangent,rest.world[mesh.node]));
    }
    a->meshes.push_back(std::move(mesh));
  }
  return a;
}
void portable_animation_attach(model_slot &slot) {
  if(!slot.portable_animation) return;auto &a=*slot.portable_animation;
  slot.model.skeleton={int(a.bones.size()),a.bones.data(),a.bind_pose.data()};slot.skinned=!a.bones.empty();
  slot.morph_names=a.morph_names;slot.morph_defaults=a.morph_defaults;
}
std::vector<Matrix> portable_animation_bones(const model_slot &slot,const model_pose &r) {
  const auto &a=*slot.portable_animation;const auto p=pose(a,&r);std::vector<Matrix> out;for(auto joint:a.joints) out.push_back(p.world[joint]);return out;
}
void portable_animation_morphs(const model_slot &slot,const model_pose *r,std::vector<float> &out) {if(slot.portable_animation->borrowed_clips) {out.clear();return;}out=pose(*slot.portable_animation,r).morphs;}
std::shared_ptr<const RenderGeometryData> portable_animation_geometry(const model_slot &slot,unsigned index,const model_pose *requested,std::span<const std::uint16_t> indices) {
  const auto &a=*slot.portable_animation;const auto &mesh=a.meshes.at(index);const auto p=pose(a,requested);
  auto result=std::make_shared<RenderGeometryData>(mesh.base);if(!indices.empty()) result->indices.assign(indices.begin(),indices.end());
  std::vector<Matrix> palette;
  for(size_t j=0;j<mesh.joints.size();++j) palette.push_back(MatrixMultiply(MatrixMultiply(mesh.inverse_node,mesh.inverse_bind[j]),p.world[mesh.joints[j]]));
  const auto node=MatrixMultiply(mesh.inverse_node,p.world[mesh.node]);
  for(size_t i=0;i<result->vertices.size();++i) {
    auto &v=result->vertices[i];Vector3 position{v.x,v.y,v.z},normal{v.normal[0],v.normal[1],v.normal[2]},tangent{v.tangent[0],v.tangent[1],v.tangent[2]};
    for(size_t k=0;k<mesh.morphs.size();++k) {const float weight=p.node_morphs[mesh.node][k];position=Vector3Add(position,Vector3Scale(mesh.dpos[k][i],weight));normal=Vector3Add(normal,Vector3Scale(mesh.dnormal[k][i],weight));tangent=Vector3Add(tangent,Vector3Scale(mesh.dtangent[k][i],weight));}
    Matrix deformation=node;
    if(!palette.empty()) {
      std::array<float,16> blend{};for(unsigned k=0;k<4;++k) {const auto m=MatrixToFloatV(palette[mesh.bone[i][k]]);for(unsigned c=0;c<16;++c) blend[c]+=m.v[c]*mesh.weights[i][k];}deformation=matrix(blend.data());
    }
    const float determinant=MatrixDeterminant(deformation);if(!std::isfinite(determinant)||std::abs(determinant)<1e-12f) throw std::runtime_error("njin animation: singular deformed vertex transform");
    position=Vector3Transform(position,deformation);normal=Vector3Normalize(direction(normal,MatrixTranspose(MatrixInvert(deformation))));tangent=direction(tangent,deformation);
    tangent=Vector3Normalize(Vector3Subtract(tangent,Vector3Scale(normal,Vector3DotProduct(normal,tangent))));
    v.x=position.x;v.y=position.y;v.z=position.z;v.normal={normal.x,normal.y,normal.z};v.tangent={tangent.x,tangent.y,tangent.z,v.tangent[3]*(determinant<0?-1.f:1.f)};
  }
  return result;
}
}
