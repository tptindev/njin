#pragma once
#include "njin_internal_only.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace njin::rhi {
struct ShaderBinaryUniform {std::string name;unsigned type,count;};
// Validate the pinned shaderc container before handing bytes to bgfx's assert-based reader.
// Compiled assets must use the same container version as the embedded engine stages.
inline std::vector<ShaderBinaryUniform> shader_binary_uniforms(std::span<const std::uint8_t> bytes,char stage,
                                                              unsigned version,std::string_view profile) {
  const auto invalid=[] {throw std::invalid_argument("njin shader: invalid compiled stage container/profile");};
  if(bytes.size()<26||bytes.size()>16*1024*1024||bytes[0]!=stage||bytes[1]!='S'||bytes[2]!='H'||bytes[3]!=version||version!=14) invalid();
  size_t at=12;
  const auto read=[&](unsigned count)->std::uint32_t {
    if(count>4||at>bytes.size()||count>bytes.size()-at) invalid();
    std::uint32_t value=0;for(unsigned i=0;i<count;++i) value|=std::uint32_t(bytes[at++])<<(8*i);return value;
  };
  if(read(4)||read(4)) throw std::invalid_argument("njin shader: raw buffer bindings are not supported by the fragment asset API");
  const auto count=read(2);if(count>512) invalid();
  std::vector<ShaderBinaryUniform> uniforms;uniforms.reserve(count);
  for(unsigned i=0;i<count;++i) {
    const auto length=read(1);if(!length||length>bytes.size()-at) invalid();
    std::string name(reinterpret_cast<const char*>(bytes.data()+at),length);at+=length;
    if(name.find('\0')!=std::string::npos) invalid();
    const auto type=read(1)&15;const auto num=read(2);read(2);read(2);read(1);const auto dimension=read(1);read(2);
    if((type!=0&&type!=2&&type!=3&&type!=4)||num>256||(type==0&&dimension!=2)) invalid();
    if(std::any_of(uniforms.begin(),uniforms.end(),[&](const auto &u){return u.name==name;})) invalid();
    uniforms.push_back({std::move(name),type,std::max(1u,num)});
  }
  const auto size=read(4);if(!size||at>=bytes.size()||size>=bytes.size()-at||bytes[at+size]!=0) invalid();
  const auto code=bytes.subspan(at,size);
  if(profile=="spirv") {
    if(size<20||size%4||code[0]!=3||code[1]!=2||code[2]!=35||code[3]!=7) invalid();
    const auto word=[&](size_t index) {const auto p=index*4;return std::uint32_t(code[p])|std::uint32_t(code[p+1])<<8|std::uint32_t(code[p+2])<<16|std::uint32_t(code[p+3])<<24;};
    if(!word(3)||word(4)) invalid();
    for(size_t i=5;i<size/4;) {const auto words=word(i)>>16;if(!words||words>size/4-i) invalid();i+=words;}
  } else if(profile=="dx11") {
    if(size<32||std::string_view(reinterpret_cast<const char*>(code.data()),4)!="DXBC") invalid();
  } else {
    // GLSL/ESSL/Metal source containers are UTF-8 text, with a separate trailing NUL.
    for(auto c:code) if(!c||(c<32&&c!='\n'&&c!='\r'&&c!='\t')) invalid();
  }
  return uniforms;
}
}
