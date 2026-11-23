#include <njin.h>
#include "fixture.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <csignal>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
volatile std::sig_atomic_t cancelled=0;
void cancel(int) {cancelled=1;}
float scalar(const njin::json_value &v) {
  if(v.kind!=njin::json_value::number) throw std::runtime_error("scene field must be a number");
  return v.f32_or(0);
}
njin::LightmapScene load(const char *path) {
  njin::json_value doc;if(!njin::json_load(path,doc)) throw std::runtime_error("cannot read scene JSON");
  njin::LightmapScene s;s.identity=doc["identity"].string_or("");
  for(unsigned c=0;c<3;++c) {s.direction[c]=scalar(doc["direction"][c]);s.illuminance[c]=scalar(doc["illuminance"][c]);}
  for(size_t i=0;i<doc["instances"].size();++i) {
    const auto &j=doc["instances"][i];njin::LightmapInstance instance;
    const double id=j["id"].number_or(0);if(id<1||id>9007199254740991.||id!=std::floor(id)) throw std::runtime_error("instance id must be a positive exact integer");
    instance.identity=uint64_t(id);instance.receiver=j["receiver"].bool_or(true);instance.dynamic=j["dynamic"].bool_or(false);
    for(unsigned c=0;c<3;++c) {
      if(j["albedo"].size()) instance.albedo[c]=scalar(j["albedo"][c]);
      if(j["emission"].size()) instance.emission[c]=scalar(j["emission"][c]);
      if(j["emissionFactor"].size()) instance.emission_factor[c]=scalar(j["emissionFactor"][c]);
    }
    const auto image=[&](const njin::json_value &description) {
      njin::LightmapTexture texture;const std::string name=description["path"].string_or("");if(name.empty()) return texture;
      const auto file=(std::filesystem::path(path).parent_path()/name).string();texture.image=std::make_shared<njin::RenderImageAsset>(njin::renderer_load_image(file.c_str()));
      texture.uv.set=unsigned(description["uvSet"].int_or(0));texture.linear=description["linear"].bool_or(true);
      if(description["transform"].size()) {if(description["transform"].size()!=6) throw std::runtime_error("UV transform needs 6 values");for(unsigned c=0;c<6;++c) texture.uv.values[c]=scalar(description["transform"][c]);}
      const auto wrap=[](const njin::json_value &v) {const std::string s=v.string_or("repeat");if(s=="repeat") return njin::RenderWrap::Repeat;if(s=="clamp") return njin::RenderWrap::Clamp;if(s=="mirror") return njin::RenderWrap::Mirror;throw std::runtime_error("invalid material wrap");};
      texture.wrap_s=wrap(description["wrapS"]);texture.wrap_t=wrap(description["wrapT"]);return texture;
    };
    instance.albedo_texture=image(j["albedoImage"]);instance.emission_texture=image(j["emissionImage"]);
    if(j["transform"].size()) {if(j["transform"].size()!=16) throw std::runtime_error("transform needs 16 values");for(unsigned c=0;c<16;++c) instance.placement.transform[c]=scalar(j["transform"][c]);}
    auto g=std::make_shared<njin::RenderGeometryData>();
    for(size_t v=0;v<j["vertices"].size();++v) {
      const auto &row=j["vertices"][v];if(row.size()!=8&&row.size()!=12) throw std::runtime_error("vertex needs x y z nx ny nz lightmapU lightmapV [u0 v0 u1 v1]");
      njin::RenderVertex vertex{scalar(row[0]),scalar(row[1]),scalar(row[2]),0,0};
      for(unsigned c=0;c<3;++c) vertex.normal[c]=scalar(row[c+3]);vertex.lightmap_uv={scalar(row[6]),scalar(row[7])};
      if(row.size()==12) {vertex.u=scalar(row[8]);vertex.v=scalar(row[9]);vertex.uv1={scalar(row[10]),scalar(row[11])};}
      g->vertices.push_back(vertex);
    }
    for(size_t k=0;k<j["indices"].size();++k) {double index=j["indices"][k].number_or(-1);if(index<0||index>65535||index!=std::floor(index)) throw std::runtime_error("invalid uint16 index");g->indices.push_back(uint16_t(index));}
    instance.geometry=g;s.instances.push_back(std::move(instance));
  }
  return s;
}
}
int main(int argc,char **argv) {
  try {
    if(argc<3) {std::fprintf(stderr,"usage: njin_lightmap <scene.json|--fixture> output.nlm [size] [samples] [seed]\n");return 2;}
    auto scene=std::string(argv[1])=="--fixture"?njin::lightmap_fixture::scene():load(argv[1]);
    njin::LightmapSettings settings;
    if(argc>3) settings.size=unsigned(std::stoul(argv[3]));if(argc>4) settings.samples=unsigned(std::stoul(argv[4]));if(argc>5) settings.seed=unsigned(std::stoul(argv[5]));
    for(int i=6;i<argc;++i) {
      const std::string option=argv[i];
      if(option=="--unwrap") settings.generate_uv=true;
      else if(option.starts_with("--bounces=")) settings.indirect_bounces=unsigned(std::stoul(option.substr(10)));
      else if(option.starts_with("--paths=")) settings.indirect_samples=unsigned(std::stoul(option.substr(8)));
      else if(option.starts_with("--density=")) settings.texels_per_unit=std::stof(option.substr(10));
      else throw std::runtime_error("unknown bake option");
    }
    std::signal(SIGINT,cancel);auto start=std::chrono::steady_clock::now();int last=-1;
    auto asset=njin::lightmap_bake(scene,settings,[&](float progress){int percent=int(progress*100);if(percent/10!=last) {last=percent/10;std::fprintf(stderr,"bake %d%%\n",percent);}return !cancelled;});
    njin::lightmap_save(argv[2],asset);
    size_t bytes=0;for(const auto &p:asset.pages) bytes+=p.image.pixels.size();
    std::printf("baked %zu pages, %zu texel bytes, %.3f ms, fingerprint %llu\n",asset.pages.size(),bytes,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),(unsigned long long)asset.fingerprint);
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
