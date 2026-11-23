#include "njin_shader.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_texture.h"
#include "njin_json.h"
#include "njin_file.h"
#include "njin_path.h"
#include "njin_log.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>
namespace njin {
namespace {
[[noreturn]] void invalid(const std::string &what) {throw std::runtime_error("njin shader: "+what);}
std::vector<std::uint8_t> binary(const std::string &path) {
  std::string bytes;
  if(!file_read(path.c_str(),bytes)||bytes.empty()||bytes.size()>16*1024*1024) invalid("missing, empty or oversized binary: "+path);
  return {bytes.begin(),bytes.end()};
}
std::string required_string(const json_value &value,const char *what) {
  if(!value.is(json_value::string)||value.str.empty()||value.str.find('\0')!=std::string::npos) invalid("invalid "+std::string(what));
  return value.str;
}
unsigned integer(const json_value &value,unsigned lo,unsigned hi,const char *what) {
  if(!value.is(json_value::number)||!std::isfinite(value.num)||value.num<lo||value.num>hi||std::floor(value.num)!=value.num) invalid("invalid "+std::string(what));
  return unsigned(value.num);
}
void available(shader_store &store,const shader_slot &slot) {
  auto &window=ctx_ref<window_guard>(*store.portable_context);
  // A 3D queue resolves uniform values at end_3d; do not invalidate its program or samplers.
  if(window.renderer_resources.world3d_open || window.graphics->shader_in_use(slot.portable))
    throw std::logic_error("njin shader: reload/unload before submitting draws or after the frame ends");
}
shader_slot load(shader_store &store,const std::string &path) {
  json_value root;
  if(!json_load(path.c_str(),root)) invalid("cannot read package: "+path);
  if(integer(root["version"],1,1,"package version")!=1) invalid("unsupported package version");
  auto &device=*ctx_ref<window_guard>(*store.portable_context).graphics;
  const auto &profile=root["profiles"][device.shader_profile()];
  const auto folder=std::filesystem::path(path).parent_path();
  const auto resolve=[&](const json_value &v){return (folder/std::filesystem::path(required_string(v,"stage path"))).lexically_normal().string();};
  shader_slot slot;slot.fs_path=path;slot.portable_files.push_back(path);
  const auto fragment_path=resolve(profile["fragment"]);
  slot.portable_files.push_back(fragment_path);
  // Custom vertex deformation needs separate shadow/normal/velocity variants. Refuse it explicitly.
  if(profile.has("vertex")) invalid("custom vertex stages require shadow/normal/velocity variants; only the engine vertex stage is supported currently");
  const auto fragment=binary(fragment_path);
  const auto info=device.create_shader({},fragment);slot.portable=info.handle;
  try {
    std::unordered_set<std::string> aliases,names;std::unordered_set<unsigned> stages;
    const auto reflected=[&](const std::string &name,rhi::ShaderUniformInfo::Type type,unsigned count) {
      const auto it=std::find_if(info.uniforms.begin(),info.uniforms.end(),[&](const auto &u){return u.name==name;});
      if(it==info.uniforms.end()||it->type!=type||it->count<count) invalid("uniform reflection mismatch: "+name);
      if(name.starts_with("u_njin")||name.starts_with("s_njin")) invalid("reserved engine uniform: "+name);
      if(!names.insert(name).second) invalid("duplicate uniform: "+name);
    };
    if(!root["uniforms"].is(json_value::null)&&!root["uniforms"].is(json_value::object)) invalid("uniforms must be an object");
    if(root["uniforms"].size()>128) invalid("too many uniforms");
    for(const auto &[alias,value]:root["uniforms"].members) {
      if(alias.empty()||!aliases.insert(alias).second) invalid("duplicate/empty alias");
      portable_shader_uniform u;u.alias=alias;u.name=required_string(value["name"],"uniform name");u.type=required_string(value["type"],"uniform type");
      if(u.type!="float"&&u.type!="int"&&u.type!="vec2"&&u.type!="vec3"&&u.type!="vec4") invalid("unsupported uniform type: "+u.type);
      u.count=value.has("count")?integer(value["count"],1,256,"uniform count"):1;
      if(u.type!="vec4"&&u.count!=1) invalid("only vec4 arrays are supported");
      reflected(u.name,rhi::ShaderUniformInfo::Type::Vec4,u.count);u.values.resize(u.count);
      slot.portable_uniforms.push_back(std::move(u));
    }
    if(!root["samplers"].is(json_value::object)||root["samplers"].size()>5) invalid("samplers must declare base texture and at most four extras");
    for(const auto &[alias,value]:root["samplers"].members) {
      if(alias.empty()||!aliases.insert(alias).second) invalid("duplicate/empty alias");
      portable_shader_sampler sampler{alias,required_string(value["name"],"sampler name"),integer(value["stage"],0,4,"sampler stage")};
      if(!stages.insert(sampler.stage).second) invalid("duplicate sampler stage");
      reflected(sampler.name,rhi::ShaderUniformInfo::Type::Sampler,1);
      slot.portable_samplers.push_back(std::move(sampler));
    }
    if(!stages.contains(0)) invalid("base sampler stage 0 is required");
    // Unspecified custom uniforms could retain another program's values in bgfx's global registry.
    for(const auto &u:info.uniforms) if(!u.name.starts_with("u_njin")&&!u.name.starts_with("s_njin")&&!names.contains(u.name)) invalid("reflected uniform is missing from package: "+u.name);
    slot.alive=true;return slot;
  } catch(...) {device.destroy_shader(slot.portable);throw;}
}
}
shader_handle portable_shader_load(shader_store &store,const char *vs,const char *fs) {
  if(!vs&&!fs) {NJIN_WARN("shader: nothing to load, both paths are null");return {};}
  if(vs||!fs||std::filesystem::path(fs).extension()!=".njshader")
    throw std::runtime_error("njin bgfx: GLSL shader sources require conversion to a .njshader asset package (shader_load(ctx,nullptr,package))");
  const auto path=asset_path(fs);
  try {
    auto slot=load(store,path);const auto program=slot.portable;
    try {store.slots.push_back(std::move(slot));}
    catch(...) {ctx_ref<window_guard>(*store.portable_context).graphics->destroy_shader(program);throw;}
    return {.id=u32(store.slots.size())};
  } catch(const std::exception &error) {NJIN_WARN("shader: %s",error.what());return {};}
}
bool portable_shader_reload(shader_store &store,shader_handle handle) {
  auto *slot=shader_slot_of(store,handle);if(!slot) return false;available(store,*slot);
  try {
    auto replacement=load(store,slot->fs_path);
    try {
      for(auto &u:replacement.portable_uniforms) for(const auto &old:slot->portable_uniforms)
        if(u.alias==old.alias&&u.type==old.type&&u.count==old.count) u.values=old.values;
      for(const auto &b:slot->textures) for(const auto &s:replacement.portable_samplers) if(b.name==s.alias) replacement.textures.push_back(b);
      ctx_ref<window_guard>(*store.portable_context).graphics->destroy_shader(slot->portable);
    } catch(...) {ctx_ref<window_guard>(*store.portable_context).graphics->destroy_shader(replacement.portable);throw;}
    *slot=std::move(replacement);return true;
  } catch(const std::exception &error) {NJIN_WARN("shader reload: %s (old program kept)",error.what());return false;}
}
void portable_shader_unload(shader_store &store,shader_handle handle) {
  auto *slot=shader_slot_of(store,handle);if(!slot) return;available(store,*slot);
  ctx_ref<window_guard>(*store.portable_context).graphics->destroy_shader(slot->portable);*slot={};
  if(store.active.id==handle.id) store.active={};
}
void portable_shader_set(shader_store &store,shader_handle handle,const char *name,const char *type,std::span<const std::array<float,4>> values) {
  auto *slot=shader_slot_of(store,handle);if(!slot||!name||values.empty()) return;
  auto it=std::find_if(slot->portable_uniforms.begin(),slot->portable_uniforms.end(),[&](const auto &u){return u.alias==name;});
  if(it==slot->portable_uniforms.end()) {if(slot->uniforms.emplace(name,-1).second) NJIN_WARN("shader: uniform '%s' is not declared in package",name);return;}
  if(it->type!=type||values.size()>it->count) throw std::invalid_argument("njin shader: uniform type/count mismatch: "+std::string(name));
  for(const auto &v:values) for(float c:v) if(!std::isfinite(c)) throw std::invalid_argument("njin shader: non-finite uniform: "+std::string(name));
  // Clear an unwritten array tail to avoid leaking an earlier, longer update.
  it->values.assign(it->count,{});std::copy(values.begin(),values.end(),it->values.begin());
}
std::shared_ptr<const RenderShaderBindings> portable_shader_bindings(const context &ctx,shader_handle handle) {
  const auto *slot=shader_slot_of(ctx_ref<shader_store>(ctx),handle);if(!slot||!slot->portable) return {};
  auto bindings=std::make_shared<RenderShaderBindings>();bindings->program=slot->portable;
  for(const auto &u:slot->portable_uniforms) bindings->uniforms.push_back({u.name,u.values});
  for(const auto &sampler:slot->portable_samplers) {
    RenderShaderTexture texture{sampler.name,sampler.stage,{},false};
    for(const auto &b:slot->textures) if(b.name==sampler.alias) {
      if(b.is_render) {
        const auto *target=render_texture_slot_of(ctx_ref<render_texture_store>(ctx),render_texture_handle{b.id});
        if(!target||!target->portable) throw std::invalid_argument("njin shader: stale render texture binding");
        texture.texture=renderer_target_texture(const_cast<context&>(ctx),target->portable);texture.linear=target->filter!=filter_nearest;
      } else {
        const auto *image=texture_slot_of(ctx_ref<texture_store>(ctx),texture_handle{b.id});
        if(!image||!image->portable||image->packed) throw std::invalid_argument("njin shader: stale or packed texture binding");
        texture.texture=image->portable;texture.linear=image->filter!=filter_nearest;
      }
    }
    bindings->textures.push_back(std::move(texture));
  }
  return bindings;
}
portable_shader_scope::portable_shader_scope(const context &ctx,shader_handle handle,bool replace):store(ctx_ref<shader_store>(ctx)),previous(store.active) {
  if((replace||!store.active.id)&&shader_slot_of(store,handle)) store.active=handle;
}
portable_shader_scope::~portable_shader_scope() {store.active=previous;}
}
