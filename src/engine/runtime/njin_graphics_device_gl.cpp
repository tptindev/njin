#include "njin_graphics_device.h"
#include "njin_renderer_resources.h"
#include "njin2rl.h"
#include <raylib.h>
#include <rlgl.h>
#include <stdexcept>
#include <algorithm>
#include "njin_surface_generated.h"
#include "njin_shadow_uniforms.h"
#include "njin_material_uniforms.h"
#include "njin_environment_uniforms.h"
#include "njin_gl_sampling.h"
#include "njin_image_mips.h"
#include <string>
namespace njin::rhi {
namespace {
class RaylibGraphicsDevice final : public GraphicsDevice {
  RendererResources &resources;
  GlSampling sampling;
  RenderTargetHandle active_target{};
  Matrix target_projection{},target_modelview{};
  Shader cutoff_shader{};
  int cutoff_location=-1,emission_location=-1,world_location=-1,normal_location=-1,light_location=-1,material_location=-1,output_location=-1,local_location=-1;
  int shadow_rows=-1,shadow_rect=-1,shadow_meta=-1,shadow_info=-1,shadow_splits=-1,shadow_forward=-1,shadow_sampler=-1;
  int post_uniform=-1,post_extra=-1,post_aux=-1,post_projection=-1,post_inverse=-1;
  int post_second=-1,post_config=-1,post_sky=-1,post_sun=-1,mask_uniform=-1,view_uniform=-1;
  int env_sh=-1,env_params=-1,env_mode=-1;
  int map_uv=-1,map_factors=-1,map_emission=-1; std::array<int,4> map_samplers{};
  Shader alpha_shader(const MeshDraw &draw) {
    if(!cutoff_shader.id) {
      const char *vertex=R"GLSL(#version 330
in vec3 vertexPosition; in vec2 vertexTexCoord; in vec2 vertexTexCoord2; in vec4 vertexTangent; in vec4 vertexColor; in vec3 vertexNormal;
uniform mat4 mvp; uniform mat4 u_njinWorld; uniform mat4 u_njinNormal;
out vec2 fragTexCoord; out vec2 fragTexCoord1; out vec4 v_tangent; out vec4 fragColor; out vec3 v_world; out vec3 v_normal;
void main() {
  gl_Position=mvp*vec4(vertexPosition,1); fragTexCoord=vertexTexCoord; fragTexCoord1=vertexTexCoord2;
  v_tangent=vec4((u_njinWorld*vec4(vertexTangent.xyz,0)).xyz,vertexTangent.w); fragColor=vertexColor;
  v_world=(u_njinWorld*vec4(vertexPosition,1)).xyz; v_normal=(u_njinNormal*vec4(vertexNormal,0)).xyz;
})GLSL";
      const std::string fragment=std::string(R"GLSL(#version 330
in vec2 fragTexCoord; in vec2 fragTexCoord1; in vec4 v_tangent; in vec4 fragColor; in vec3 v_world; in vec3 v_normal; out vec4 finalColor;
uniform sampler2D texture0; uniform vec4 colDiffuse; uniform vec2 alphaParams;
uniform sampler2D normalMap,metalRoughMap,occlusionMap,emissionMap;
#define NJIN_NORMAL_TEX(uv) texture(normalMap,uv)
#define NJIN_MR_TEX(uv) texture(metalRoughMap,uv)
#define NJIN_AO_TEX(uv) texture(occlusionMap,uv)
#define NJIN_EMISSIVE_TEX(uv) texture(emissionMap,uv)
uniform vec4 u_njinMapUv[10],u_njinMapFactors,u_njinMapEmission;
uniform sampler2D environmentBase,environmentAtlas;
#define NJIN_ATAN2(y,x) atan(y,x)
#define NJIN_ENV_BASE(uv) texture(environmentBase,uv)
#define NJIN_ENV_ATLAS(uv) texture(environmentAtlas,uv)
uniform vec4 u_njinEnvSH[9],u_njinEnvParams,u_njinEnvMode;
uniform sampler2D postMap;
#define NJIN_POST_COLOR(uv) textureLod(texture0,uv,0.0)
#define NJIN_POST_AUX(uv) textureLod(postMap,uv,0.0)
uniform sampler2D postSecond;
#define NJIN_POST_SECOND(uv) textureLod(postSecond,uv,0.0)
uniform vec4 u_njinTransparency;uniform mat4 u_njinTransparencyVP;
#define NJIN_OPAQUE_DEPTH(uv) textureLod(texture0,uv,0.0)
#define NJIN_VELOCITY(uv) textureLod(texture0,uv,0.0)
uniform vec4 u_njinPost,u_njinPostExtra,u_njinPostConfig,u_njinPostSky,u_njinPostSun,u_njinMask;
uniform mat4 u_njinView;
uniform mat4 u_njinPostProjection,u_njinPostInverse;
uniform mat4 u_njinPreviousClip,u_njinPreviousView;
uniform vec4 u_njinTemporal;
#define mul(a,b) ((a)*(b))
uniform sampler2D shadowMap;
#define NJIN_SHADOW_TEX(uv) texture(shadowMap,uv)
uniform vec4 u_njinShadowRows[32],u_njinShadowRect[28],u_njinShadowMeta[28];
uniform vec4 u_njinShadowInfo,u_njinShadowSplits,u_njinShadowForward;
uniform vec4 u_njinEmission; uniform vec4 u_njinLight[4]; uniform vec4 u_njinMaterial; uniform vec4 u_njinOutput; uniform vec4 u_njinLocal[64];
uniform vec4 u_njinSurfaceFx[4]; // zero-initialized: portable extensions stay disabled in this adapter
)GLSL")+njin_surface_glsl+R"GLSL(
void main() {
  if(u_njinPost.x>0.5) {finalColor=vec4(njinPostColor(fragTexCoord),1.0);return;}
  vec4 texel=texture(texture0,njinMapUv(4,fragTexCoord,fragTexCoord1)); vec4 c=texel*colDiffuse*fragColor;
  if(c.a<alphaParams.x) discard;
  if(u_njinShadowInfo.w<0.0) { finalColor=vec4(njinPackDepth(gl_FragCoord.z),1.0); return; }
  if(u_njinMask.x>0.5) {finalColor=njinNormalMask(v_normal,v_world,v_tangent,fragTexCoord,fragTexCoord1,vec4(0.0));return;}
  c.a=mix(c.a,1.0,alphaParams.y);
  vec3 linearBase=njinLinear(texel.rgb)*njinLinear(colDiffuse.rgb)*njinLinear(fragColor.rgb);
  c.rgb=njinSurface(c.rgb,linearBase,v_normal,v_world,fragTexCoord,fragTexCoord1,fragTexCoord1,v_tangent,vec4(0.0),vec3(0.0)); finalColor=c;
})GLSL";
      cutoff_shader=LoadShaderFromMemory(vertex,fragment.c_str());
      if(!IsShaderValid(cutoff_shader)) throw std::runtime_error("njin OpenGL: alpha cutoff shader failed");
      shadow_rows=GetShaderLocation(cutoff_shader,"u_njinShadowRows[0]");
      shadow_rect=GetShaderLocation(cutoff_shader,"u_njinShadowRect[0]");
      shadow_meta=GetShaderLocation(cutoff_shader,"u_njinShadowMeta[0]");
      shadow_info=GetShaderLocation(cutoff_shader,"u_njinShadowInfo");
      shadow_splits=GetShaderLocation(cutoff_shader,"u_njinShadowSplits");
      shadow_forward=GetShaderLocation(cutoff_shader,"u_njinShadowForward");
      shadow_sampler=GetShaderLocation(cutoff_shader,"shadowMap");
      cutoff_shader.locs[SHADER_LOC_MAP_METALNESS]=shadow_sampler;
      post_second=GetShaderLocation(cutoff_shader,"postSecond");post_config=GetShaderLocation(cutoff_shader,"u_njinPostConfig");post_sky=GetShaderLocation(cutoff_shader,"u_njinPostSky");
      post_sun=GetShaderLocation(cutoff_shader,"u_njinPostSun");
      mask_uniform=GetShaderLocation(cutoff_shader,"u_njinMask");view_uniform=GetShaderLocation(cutoff_shader,"u_njinView");
      post_projection=GetShaderLocation(cutoff_shader,"u_njinPostProjection");post_inverse=GetShaderLocation(cutoff_shader,"u_njinPostInverse");
      post_uniform=GetShaderLocation(cutoff_shader,"u_njinPost");post_extra=GetShaderLocation(cutoff_shader,"u_njinPostExtra");post_aux=GetShaderLocation(cutoff_shader,"postMap");
      env_sh=GetShaderLocation(cutoff_shader,"u_njinEnvSH[0]");env_params=GetShaderLocation(cutoff_shader,"u_njinEnvParams");env_mode=GetShaderLocation(cutoff_shader,"u_njinEnvMode");
      cutoff_shader.locs[SHADER_LOC_MAP_HEIGHT]=GetShaderLocation(cutoff_shader,"environmentBase");
      cutoff_shader.locs[SHADER_LOC_MAP_BRDF]=GetShaderLocation(cutoff_shader,"environmentAtlas");
      map_uv=GetShaderLocation(cutoff_shader,"u_njinMapUv[0]");
      map_factors=GetShaderLocation(cutoff_shader,"u_njinMapFactors");
      map_emission=GetShaderLocation(cutoff_shader,"u_njinMapEmission");
      const char *names[]{"normalMap","metalRoughMap","occlusionMap","emissionMap"};
      for(unsigned i=0;i<4;++i) { map_samplers[i]=GetShaderLocation(cutoff_shader,names[i]); cutoff_shader.locs[SHADER_LOC_MAP_NORMAL+i]=map_samplers[i]; }
      cutoff_shader.locs[SHADER_LOC_VERTEX_TANGENT]=GetShaderLocationAttrib(cutoff_shader,"vertexTangent");
      cutoff_shader.locs[SHADER_LOC_VERTEX_TEXCOORD02]=GetShaderLocationAttrib(cutoff_shader,"vertexTexCoord2");
      cutoff_location=GetShaderLocation(cutoff_shader,"alphaParams");
      world_location=GetShaderLocation(cutoff_shader,"u_njinWorld");
      normal_location=GetShaderLocation(cutoff_shader,"u_njinNormal");
      light_location=GetShaderLocation(cutoff_shader,"u_njinLight[0]");
      local_location=GetShaderLocation(cutoff_shader,"u_njinLocal[0]");
      material_location=GetShaderLocation(cutoff_shader,"u_njinMaterial");
      output_location=GetShaderLocation(cutoff_shader,"u_njinOutput");
      emission_location=GetShaderLocation(cutoff_shader,"u_njinEmission");
      if(local_location<0 || material_location<0 || output_location<0 || world_location<0 || normal_location<0 || light_location<0 || emission_location<0 || cutoff_location<0) throw std::runtime_error("njin OpenGL: alpha cutoff shader is unavailable");
    }
    const float values[]{draw.alpha_cutoff,draw.opaque_alpha ? 1.0f:0.0f};
    SetShaderValue(cutoff_shader,cutoff_location,values,SHADER_UNIFORM_VEC2);
    const float glow[]{draw.emission[0],draw.emission[1],draw.emission[2],draw.emission_strength};
    SetShaderValue(cutoff_shader,emission_location,glow,SHADER_UNIFORM_VEC4);
    const auto matrix=[](const std::array<float,16> &v){return Matrix{v[0],v[4],v[8],v[12],v[1],v[5],v[9],v[13],v[2],v[6],v[10],v[14],v[3],v[7],v[11],v[15]};};
    SetShaderValueMatrix(cutoff_shader,world_location,matrix(draw.world));
    SetShaderValueMatrix(cutoff_shader,normal_location,matrix(draw.normal_transform));
    const auto &l=draw.lighting;
    const float light[16]{l.direction[0],l.direction[1],l.direction[2],l.enabled?1.0f:0.0f,
      l.color[0],l.color[1],l.color[2],l.specular,l.ambient[0],l.ambient[1],l.ambient[2],l.shininess,
      l.camera[0],l.camera[1],l.camera[2],l.intensity};
    SetShaderValueV(cutoff_shader,light_location,light,SHADER_UNIFORM_VEC4,4);
    const float mat[]{l.metallic,l.roughness,l.pbr?1.0f:0.0f,float(draw.tonemap)};
    const float output[]{draw.exposure,draw.linear_output?1.0f:0.0f,draw.resolve?1.0f:0.0f,float(draw.local_light_count)};
    SetShaderValue(cutoff_shader,material_location,mat,SHADER_UNIFORM_VEC4);
    SetShaderValue(cutoff_shader,output_location,output,SHADER_UNIFORM_VEC4);
      std::array<float,256> locals{};
      for(unsigned i=0;i<draw.local_light_count;++i) {
        const auto &a=draw.local_lights[i]; const unsigned k=i*16;
        const float values[]{a.position[0],a.position[1],a.position[2],a.radius,
          a.color[0],a.color[1],a.color[2],a.intensity,a.direction[0],a.direction[1],a.direction[2],a.cos_outer,
          a.cos_inner,a.spot?1.0f:0.0f,draw.shadows?float(draw.shadows->local_base[i]+1):0.0f,0};
        std::copy_n(values,16,locals.begin()+k);
      }
    SetShaderValueV(cutoff_shader,local_location,locals.data(),SHADER_UNIFORM_VEC4,64);
    const float mask_values[]{draw.normal_pass?1.f:0.f,draw.reflectivity,0,0},post_config_values[]{draw.post.thickness,draw.post.edge_cos,draw.post.fade,draw.post.halo};const int second_unit=6;
    SetShaderValue(cutoff_shader,mask_uniform,mask_values,SHADER_UNIFORM_VEC4);SetShaderValueMatrix(cutoff_shader,view_uniform,matrix(draw.view));
    SetShaderValue(cutoff_shader,post_config,post_config_values,SHADER_UNIFORM_VEC4);SetShaderValue(cutoff_shader,post_sky,draw.post.sky.data(),SHADER_UNIFORM_VEC4);SetShaderValue(cutoff_shader,post_sun,draw.post.sun.data(),SHADER_UNIFORM_VEC4);SetShaderValue(cutoff_shader,post_second,&second_unit,SHADER_UNIFORM_INT);
    const float post_values[]{float(draw.post.mode),draw.post.texel[0],draw.post.texel[1],draw.post.amount};
    const float post_info[]{draw.post.levels,(draw.post.mode==RenderPostMode::Resolve?draw.post.auto_exposure:draw.post.mode==RenderPostMode::ExposureAdapt?draw.post.reset:draw.post.karis)?1.f:0.f,draw.post.radius,draw.post.samples};const int post_unit=10;
    SetShaderValue(cutoff_shader,post_uniform,post_values,SHADER_UNIFORM_VEC4);SetShaderValue(cutoff_shader,post_extra,post_info,SHADER_UNIFORM_VEC4);SetShaderValue(cutoff_shader,post_aux,&post_unit,SHADER_UNIFORM_INT);
    SetShaderValueMatrix(cutoff_shader,post_projection,matrix(draw.post.projection));SetShaderValueMatrix(cutoff_shader,post_inverse,matrix(draw.post.inverse_projection));
    const EnvironmentUniforms environment(draw);
    SetShaderValueV(cutoff_shader,env_sh,environment.sh.data(),SHADER_UNIFORM_VEC4,9);
    SetShaderValue(cutoff_shader,env_params,environment.params.data(),SHADER_UNIFORM_VEC4);
    SetShaderValue(cutoff_shader,env_mode,environment.mode.data(),SHADER_UNIFORM_VEC4);
    const ShadowUniforms shadows(draw);
    SetShaderValueV(cutoff_shader,shadow_rows,shadows.rows.data(),SHADER_UNIFORM_VEC4,32);
    SetShaderValueV(cutoff_shader,shadow_rect,shadows.regions.data(),SHADER_UNIFORM_VEC4,28);
    SetShaderValueV(cutoff_shader,shadow_meta,shadows.metadata.data(),SHADER_UNIFORM_VEC4,28);
    SetShaderValue(cutoff_shader,shadow_info,shadows.info.data(),SHADER_UNIFORM_VEC4);
    SetShaderValue(cutoff_shader,shadow_splits,shadows.splits.data(),SHADER_UNIFORM_VEC4);
    SetShaderValue(cutoff_shader,shadow_forward,shadows.forward.data(),SHADER_UNIFORM_VEC4);
    if(draw.shadows) SetShaderValueTexture(cutoff_shader,shadow_sampler,resources.textures.at(draw.shadows->texture.id).gl);
    const MaterialUniforms mapped(draw);
    SetShaderValueV(cutoff_shader,map_uv,mapped.uv.data(),SHADER_UNIFORM_VEC4,10);
    SetShaderValue(cutoff_shader,map_factors,mapped.factors.data(),SHADER_UNIFORM_VEC4);
    SetShaderValue(cutoff_shader,map_emission,mapped.emission.data(),SHADER_UNIFORM_VEC4);
    return cutoff_shader;
  }
public:
  explicit RaylibGraphicsDevice(RendererResources &r):resources(r) {}
  ~RaylibGraphicsDevice() override { if(cutoff_shader.id) UnloadShader(cutoff_shader); }
  void begin_frame(rgba clear) override {
    if(frame_active) throw std::logic_error("njin OpenGL: nested frame");
    Color color{}; to_raylib(clear,color); BeginDrawing(); ClearBackground(color); frame_active=true;++frame_counter;
  }
  void end_frame(RenderImageAsset *capture) override {
    if(active_target) throw std::logic_error("njin OpenGL: render target must end before the frame");
    require_frame(); frame_active=false;
    if(capture) {
      if(IsWindowMinimized() || IsWindowHidden())
        throw std::runtime_error("njin OpenGL: window capture requires a visible, non-minimized window");
      rlDrawRenderBatchActive();
      // A screen capture reads the window, regardless of the read framebuffer
      // left by legacy depth copies or offscreen rendering.
      if(rlGetVersion()!=RL_OPENGL_ES_20) rlBindFramebuffer(RL_READ_FRAMEBUFFER,0);
      rlCheckErrors();
      Image image=LoadImageFromScreen();
      rlCheckErrors();
      struct Free { Image image; ~Free() { UnloadImage(image); } } free{image};
      if(!image.data) throw std::runtime_error("njin OpenGL capture failed");
      capture->width=image.width; capture->height=image.height;
      const auto *pixels=(const unsigned char*)image.data;
      capture->pixels.assign(pixels,pixels+size_t(image.width)*image.height*4);
    }
    EndDrawing();
  }
  void destroy_texture(RenderTextureHandle handle) override {
    auto &slot=resources.textures.at(handle.id);
    if(slot.gl.id) UnloadTexture(slot.gl);
    slot.gl={};
  }
  void destroy_mesh(RenderMeshHandle handle) override {
    auto &slot=resources.meshes.at(handle.id);
    if(slot.gl.vertices) UnloadMesh(slot.gl);
    slot.gl={};
  }
  void create_target(RenderTargetHandle handle,RenderTextureHandle color,unsigned width,unsigned height) override {
    auto target=LoadRenderTexture((int)width,(int)height);
    if(!IsRenderTextureValid(target)) {
      if(target.id) UnloadRenderTexture(target);
      throw std::runtime_error("njin OpenGL: render target allocation failed");
    }
    resources.targets.at(handle.id).gl=target;
    resources.textures.at(color.id).gl=target.texture;
  }
  void create_hdr_target(RenderTargetHandle handle,RenderTextureHandle color,unsigned width,unsigned height) override {
    if(active_target) throw std::logic_error("njin OpenGL: allocate HDR targets outside active target passes");
    RenderTexture2D target{};
    target.id=rlLoadFramebuffer();
    if(!target.id) throw std::runtime_error("njin OpenGL: HDR framebuffer allocation failed");
    target.texture={rlLoadTexture(nullptr,(int)width,(int)height,PIXELFORMAT_UNCOMPRESSED_R16G16B16A16,1),
      (int)width,(int)height,1,PIXELFORMAT_UNCOMPRESSED_R16G16B16A16};
    target.depth={rlLoadTextureDepth((int)width,(int)height,true),(int)width,(int)height,1,19};
    rlFramebufferAttach(target.id,target.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
    rlFramebufferAttach(target.id,target.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_RENDERBUFFER,0);
    const bool valid=target.texture.id&&target.depth.id&&rlFramebufferComplete(target.id);
    rlDisableFramebuffer();
    if(!valid) { UnloadRenderTexture(target); throw std::runtime_error("njin OpenGL: RGBA16F HDR target unavailable"); }
    resources.targets.at(handle.id).gl=target; resources.textures.at(color.id).gl=target.texture;
  }
  void destroy_target(RenderTargetHandle handle,RenderTextureHandle color) override {
    auto &target=resources.targets.at(handle.id);
    if(target.gl.id) UnloadRenderTexture(target.gl);
    target.gl={}; resources.textures.at(color.id).gl={};
  }
  void begin_target(RenderTargetHandle handle,rgba clear,bool clear_enabled) override {
    require_frame();
    if(active_target || rlGetActiveFramebuffer()!=0)
      throw std::logic_error("njin OpenGL: cannot nest RHI targets inside another framebuffer pass");
    rlDrawRenderBatchActive();
    target_projection=rlGetMatrixProjection(); target_modelview=rlGetMatrixModelview();
    BeginTextureMode(resources.targets.at(handle.id).gl);
    // Canonical target textures store logical top at texture row zero, also
    // for legacy 2D calls made inside the new renderer target scope.
    const auto &target=resources.targets.at(handle.id);
    rlMatrixMode(RL_PROJECTION); rlLoadIdentity();
    rlOrtho(0,target.width,0,target.height,0,1); rlMatrixMode(RL_MODELVIEW);
    rlDisableBackfaceCulling();
    Color color{}; to_raylib(clear,color); if(clear_enabled) ClearBackground(color); active_target=handle;
  }
  void end_target() override {
    require_frame();
    if(!active_target) throw std::logic_error("njin OpenGL: no active render target");
    EndTextureMode();
    rlSetMatrixProjection(target_projection); rlSetMatrixModelview(target_modelview);
    active_target={};
    rlEnableBackfaceCulling();
  }
  std::uint64_t texture_uploads() const override { return resources.texture_uploads; }
  std::uint64_t mesh_uploads() const override { return resources.mesh_uploads; }
  void ensure_texture(RenderTextureHandle handle,const RenderImage &im) override {
    auto &slot=resources.textures.at(handle.id); if(slot.gl.id) return;
    const ImageMipChain chain(im);
    Image image{const_cast<unsigned char*>(chain.pixels.data()),int(im.width),int(im.height),int(chain.levels),im.format==RenderImageFormat::RGBA32F?PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:im.format==RenderImageFormat::RGBA16F?PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    slot.gl=LoadTextureFromImage(image);
    if(!slot.gl.id) throw std::runtime_error("njin OpenGL: material map upload failed");
    ++resources.texture_uploads;
  }
  void replace_texture(RenderTextureHandle handle,const RenderImage &im) override {
    auto &slot=resources.textures.at(handle.id);if(!slot.gl.id) return;
    const ImageMipChain chain(im);Image image{const_cast<unsigned char*>(chain.pixels.data()),int(im.width),int(im.height),int(chain.levels),im.format==RenderImageFormat::RGBA32F?PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:im.format==RenderImageFormat::RGBA16F?PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    const auto next=LoadTextureFromImage(image);if(!next.id) throw std::runtime_error("njin OpenGL: texture replacement failed");
    rlDrawRenderBatchActive();UnloadTexture(slot.gl);slot.gl=next;++resources.texture_uploads;
  }
  RenderImageAsset read_target(RenderTargetHandle handle) override {
    rlDrawRenderBatchActive();Image image=LoadImageFromTexture(resources.targets.at(handle.id).gl.texture);
    if(!image.data) throw std::runtime_error("njin OpenGL: target readback failed");
    struct Free {Image &i;~Free(){UnloadImage(i);}} free{image};
    ImageFormat(&image,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if(!image.data) throw std::runtime_error("njin OpenGL: target readback conversion failed");
    const auto *p=static_cast<unsigned char*>(image.data);
    return {unsigned(image.width),unsigned(image.height),{p,p+size_t(image.width)*image.height*4}};
  }
  void draw(const MeshDraw &draw) override {
    if(draw.clusters) throw std::runtime_error("njin: portable clustered packets require bgfx; legacy lighting retains its existing clusters");
    for(auto map:advanced_material_maps(draw)) if(map->texture) throw std::runtime_error("njin: low-level advanced material maps require bgfx; legacy materials retain their existing renderer");
    for(auto map:clearcoat_material_maps(draw)) if(map->texture) throw std::runtime_error("njin: low-level clearcoat maps require bgfx; legacy materials retain their existing renderer");
    if(draw.sky) throw std::runtime_error("njin: low-level procedural sky packets require bgfx; the legacy sky uses draw_sky3d()");
    if(draw.cover) throw std::runtime_error("njin: low-level weather cover packets require bgfx; legacy weather uses draw_sky3d()");
    if(draw.maps.advanced[0]>0||draw.maps.advanced[1]>0||draw.transmission[0][0]>0) throw std::runtime_error("njin: low-level world UV/transmission requires bgfx; legacy materials retain their existing renderer");
    if(draw.velocity_pass||draw.post.mode>=RenderPostMode::VelocityDepth) throw std::runtime_error("njin: portable temporal passes require bgfx; legacy post3d uses its existing temporal pipeline");
    if(draw.transparency_pass) throw std::runtime_error("njin: low-level OIT passes require bgfx; legacy post3d retains its existing OIT pipeline");
    if(draw.post.mode>=RenderPostMode::SMAAEdge&&draw.post.mode<=RenderPostMode::SMAABlend) throw std::runtime_error("njin: low-level SMAA passes require bgfx; legacy post3d uses its existing SMAA pipeline");
    require_frame();
    auto *persistent=draw.texture ? &resources.textures.at(draw.texture.id):nullptr;
    RendererResources::MeshResource transient;
    struct FreeTransient { RendererResources::MeshResource &r; ~FreeTransient(){if(r.gl.vertices) UnloadMesh(r.gl);} } free_transient{transient};
    auto *persistent_mesh=draw.mesh ? &resources.meshes.at(draw.mesh.id):((draw.geometry||draw.post.mode!=RenderPostMode::None||draw.environment||has_material_maps(draw))?&transient:nullptr);
    const auto &im=draw.image;
  Image image{const_cast<unsigned char *>(im.rgba.data()), (int)im.width, (int)im.height, 1, im.format==RenderImageFormat::RGBA32F?PIXELFORMAT_UNCOMPRESSED_R32G32B32A32:im.format==RenderImageFormat::RGBA16F?PIXELFORMAT_UNCOMPRESSED_R16G16B16A16:PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  auto texture = persistent ? persistent->gl : Texture2D{};
  if(!texture.id) {
    const ImageMipChain chain(im);image.data=const_cast<unsigned char*>(chain.pixels.data());image.mipmaps=int(chain.levels);
    texture=LoadTextureFromImage(image);
    if(texture.id) ++resources.texture_uploads;
    if(persistent) persistent->gl=texture;
  }
  if (!texture.id) throw std::runtime_error("njin renderer: texture upload failed");
  struct TemporaryTexture {
    Texture2D texture;
    ~TemporaryTexture() { if(texture.id) UnloadTexture(texture); }
  } temporary{persistent ? Texture2D{}:texture};
  SetTextureFilter(texture,draw.linear_sampling ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
  const Color tint{(unsigned char)(draw.tint[0]*255),(unsigned char)(draw.tint[1]*255),(unsigned char)(draw.tint[2]*255),(unsigned char)(draw.tint[3]*255)};
  rlDrawRenderBatchActive();
  const Matrix saved_projection=rlGetMatrixProjection();
  const Matrix saved_modelview=rlGetMatrixModelview();
  struct MatrixScope {
    Matrix projection,view;
    bool blend,opaque;
    bool scissor;
    bool target;
    GlSampling &sampling;
    ~MatrixScope() {
      rlDrawRenderBatchActive();
      sampling.clear();
      rlDisableDepthTest();
      rlSetCullFace(RL_CULL_FACE_BACK);
      if(target) rlDisableBackfaceCulling(); else rlEnableBackfaceCulling();
      rlSetMatrixProjection(projection); rlSetMatrixModelview(view);
      if(blend) EndBlendMode();
      if(opaque) rlEnableColorBlend();
      if(scissor) EndScissorMode();
    }
  } restore{saved_projection,saved_modelview,draw.blend!=RenderBlend::Alpha&&draw.blend!=RenderBlend::Opaque,draw.blend==RenderBlend::Opaque,draw.scissor_enabled,bool(active_target),sampling};
  if(draw.blend==RenderBlend::Opaque) rlDisableColorBlend();
  else if(draw.blend==RenderBlend::WeightedAdd||draw.blend==RenderBlend::Reveal) {
    rlSetBlendFactors(draw.blend==RenderBlend::WeightedAdd?RL_ONE:RL_ZERO,draw.blend==RenderBlend::WeightedAdd?RL_ONE:RL_ONE_MINUS_SRC_ALPHA,RL_FUNC_ADD);BeginBlendMode(BLEND_CUSTOM);
  }
  else if(draw.blend!=RenderBlend::Alpha)
    BeginBlendMode(draw.blend==RenderBlend::Additive ? BLEND_ADDITIVE:BLEND_MULTIPLIED);
  if(draw.scissor_enabled) {
    const auto &clip=draw.scissor;
    const int y=active_target ? (int)resources.targets.at(active_target.id).height-clip.y-(int)clip.height:clip.y;
    BeginScissorMode(clip.x,y,(int)clip.width,(int)clip.height);
  }
  const Matrix identity_matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const auto &m=draw.transform;
  // Store target rows in the same orientation as uploaded CPU textures.
  const float y=active_target ? -1.0f:1.0f;
  const Matrix modelview{m[0],m[4],m[8],m[12],y*m[1],y*m[5],y*m[9],y*m[13],m[2],m[6],m[10],m[14],m[3],m[7],m[11],m[15]};
  rlSetMatrixProjection(identity_matrix);
  rlSetMatrixModelview(modelview);
  if(draw.depth_test) rlEnableDepthTest(); else rlDisableDepthTest();
  if(draw.cull==RenderCull::None) rlDisableBackfaceCulling();
  else {
    rlEnableBackfaceCulling();
    const bool clockwise=(draw.cull==RenderCull::Clockwise)!=bool(active_target);
    rlSetCullFace(clockwise ? RL_CULL_FACE_BACK:RL_CULL_FACE_FRONT);
  }
  sampling.set(0,draw.linear_sampling,draw.wrap_s,draw.wrap_t,draw.color_min_filter);
  sampling.set(1,false,RenderWrap::Clamp,RenderWrap::Clamp);
  const auto mapped_textures=material_maps(draw);
  for(unsigned i=0;i<4;++i) sampling.set(i+2,mapped_textures[i]->linear,mapped_textures[i]->wrap_s,mapped_textures[i]->wrap_t,mapped_textures[i]->min_filter);
  if(persistent_mesh) {
    auto &mesh=persistent_mesh->gl;
    if(!mesh.vertices) {
      mesh.vertexCount=(int)draw.vertices.size(); mesh.triangleCount=(int)(draw.indices.empty() ? draw.vertices.size():draw.indices.size())/3;
      mesh.vertices=(float*)MemAlloc(mesh.vertexCount*3*sizeof(float));
      mesh.normals=(float*)MemAlloc(mesh.vertexCount*3*sizeof(float));
      mesh.texcoords=(float*)MemAlloc(mesh.vertexCount*2*sizeof(float));
      mesh.texcoords2=(float*)MemAlloc(mesh.vertexCount*2*sizeof(float));
      mesh.tangents=(float*)MemAlloc(mesh.vertexCount*4*sizeof(float));
      mesh.colors=(unsigned char*)MemAlloc(mesh.vertexCount*4);
      if(!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.texcoords2 || !mesh.tangents || !mesh.colors) { UnloadMesh(mesh); mesh={}; throw std::bad_alloc(); }
      if(!draw.indices.empty()) {
        mesh.indices=(unsigned short*)MemAlloc((unsigned int)(draw.indices.size()*sizeof(unsigned short)));
        if(!mesh.indices) { UnloadMesh(mesh); mesh={}; throw std::bad_alloc(); }
        std::copy(draw.indices.begin(),draw.indices.end(),mesh.indices);
      }
      for(int i=0;i<mesh.vertexCount;++i) {
        const auto &v=draw.vertices[i];
        mesh.vertices[i*3]=v.x; mesh.vertices[i*3+1]=v.y; mesh.vertices[i*3+2]=v.z;
        for(int c=0;c<3;++c) mesh.normals[i*3+c]=v.normal[c];
        mesh.texcoords[i*2]=v.u; mesh.texcoords[i*2+1]=v.v;
        for(int c=0;c<2;++c) mesh.texcoords2[i*2+c]=v.uv1[c];
        for(int c=0;c<4;++c) mesh.tangents[i*4+c]=v.tangent[c];
        for(int c=0;c<4;++c) mesh.colors[i*4+c]=(unsigned char)(v.color[c]*255);
      }
      UploadMesh(&mesh,false);
      if(!mesh.vaoId) { UnloadMesh(mesh); mesh={}; throw std::runtime_error("njin OpenGL: mesh upload failed"); }
      if(draw.mesh) ++resources.mesh_uploads;
    }
    Material material=LoadMaterialDefault();
    if(!material.maps) throw std::bad_alloc();
    material.maps[MATERIAL_MAP_DIFFUSE].texture=texture;
    material.maps[MATERIAL_MAP_DIFFUSE].color=tint;
    const Shader original_shader=material.shader;
    if(draw.normal_pass || draw.post.mode!=RenderPostMode::None || draw.environment || has_material_maps(draw) || draw.shadow_pass || draw.shadows || draw.alpha_cutoff>0 || draw.opaque_alpha || draw.resolve || draw.linear_output || draw.tonemap!=RenderToneMap::None || draw.lighting.enabled || draw.emission!=std::array<float,3>{}) material.shader=alpha_shader(draw);
    if(draw.shadows) material.maps[MATERIAL_MAP_METALNESS].texture=resources.textures.at(draw.shadows->texture.id).gl;
    for(unsigned i=0;i<4;++i) if(mapped_textures[i]->texture) material.maps[i+2].texture=resources.textures.at(mapped_textures[i]->texture.id).gl;
    if(draw.environment) {
      material.maps[MATERIAL_MAP_HEIGHT].texture=resources.textures.at(draw.environment->base.id).gl;
      material.maps[MATERIAL_MAP_BRDF].texture=resources.textures.at(draw.environment->atlas.id).gl;
      sampling.set(6,true,RenderWrap::Repeat,RenderWrap::Clamp);sampling.set(10,true,RenderWrap::Repeat,RenderWrap::Clamp);
    }
    if(draw.post.secondary) {material.maps[MATERIAL_MAP_HEIGHT].texture=resources.textures.at(draw.post.secondary.id).gl;sampling.set(6,false,RenderWrap::Clamp,RenderWrap::Clamp);}
    if(draw.post.auxiliary) {material.maps[MATERIAL_MAP_BRDF].texture=resources.textures.at(draw.post.auxiliary.id).gl;sampling.set(10,draw.post.auxiliary_linear,RenderWrap::Clamp,RenderWrap::Clamp);}
    const Matrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    DrawMesh(mesh,material,identity);
    // The slot owns the texture; UnloadMaterial otherwise releases it too.
    material.maps[MATERIAL_MAP_DIFFUSE].texture={}; material.maps[MATERIAL_MAP_METALNESS].texture={};
    for(unsigned i=2;i<6;++i) material.maps[i].texture={};
    material.maps[MATERIAL_MAP_HEIGHT].texture={}; material.maps[MATERIAL_MAP_BRDF].texture={};
    material.shader=original_shader;
    UnloadMaterial(material);
  } else {
    if(draw.normal_pass || draw.post.mode!=RenderPostMode::None || draw.environment || has_material_maps(draw) || draw.shadow_pass || draw.shadows || draw.alpha_cutoff>0 || draw.opaque_alpha || draw.resolve || draw.linear_output || draw.tonemap!=RenderToneMap::None || draw.lighting.enabled || draw.emission!=std::array<float,3>{}) BeginShaderMode(alpha_shader(draw));
    rlSetTexture(texture.id); rlBegin(RL_TRIANGLES);
    const auto vertex=[&](const RenderVertex &v) {
      rlColor4ub((unsigned char)(v.color[0]*tint.r),(unsigned char)(v.color[1]*tint.g),
        (unsigned char)(v.color[2]*tint.b),(unsigned char)(v.color[3]*tint.a));
      rlNormal3f(v.normal[0],v.normal[1],v.normal[2]);
      rlTexCoord2f(v.u,v.v); rlVertex3f(v.x,v.y,v.z);
    };
    if(draw.indices.empty()) for(const auto &v:draw.vertices) vertex(v);
    else for(auto index:draw.indices) vertex(draw.vertices[index]);
    rlEnd(); rlSetTexture(0); rlDrawRenderBatchActive();
    if(draw.normal_pass || draw.post.mode!=RenderPostMode::None || draw.environment || has_material_maps(draw) || draw.shadow_pass || draw.shadows || draw.alpha_cutoff>0 || draw.opaque_alpha || draw.resolve || draw.linear_output || draw.tonemap!=RenderToneMap::None || draw.lighting.enabled || draw.emission!=std::array<float,3>{}) EndShaderMode();
  }
  }
};
}
std::unique_ptr<GraphicsDevice> make_raylib_device(RendererResources &resources) {
  return std::make_unique<RaylibGraphicsDevice>(resources);
}
}
