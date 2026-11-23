$input v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color, v_lightmap_uv
#define NJIN_VOLUME_ONLY 1
#define NJIN_BGFX_EXTENSIONS 1
#define NJIN_CLUSTERED_LIGHTS 1
#include <bgfx_shader.sh>
SAMPLER2D(s_njinColor,0);
uniform vec4 u_njinTint;
uniform vec4 u_njinParticleFx;
uniform vec4 u_njinSurfaceFx[4];
uniform vec4 u_njinLegacyPost[10];
uniform vec4 u_njinLightEdges[64];
uniform vec4 u_njinSpriteFx[4];
uniform vec4 u_njinMipFilter[2];
uniform vec4 u_njinEmission;
uniform vec4 u_njinParams;
uniform vec4 u_njinLight[4];
uniform vec4 u_njinMaterial;
uniform vec4 u_njinOutput;
uniform vec4 u_njinLocal[64];
SAMPLER2D(s_njinShadow,1);
#define NJIN_SHADOW_TEX(uv) texture2D(s_njinShadow,uv)
uniform vec4 u_njinShadowRows[32];
uniform vec4 u_njinShadowRect[28];
uniform vec4 u_njinShadowMeta[28];
uniform vec4 u_njinShadowInfo;
uniform vec4 u_njinShadowSplits;
uniform vec4 u_njinShadowForward;
SAMPLER2D(s_njinNormalMap,2);
SAMPLER2D(s_njinMetalRoughMap,3);
SAMPLER2D(s_njinOcclusionMap,4);
SAMPLER2D(s_njinEmissionMap,5);
#define NJIN_NORMAL_TEX(uv) (u_njinMipFilter[0].x>0.5?texture2DLod(s_njinNormalMap,uv,0.0):texture2D(s_njinNormalMap,uv))
#define NJIN_MR_TEX(uv) (u_njinMipFilter[0].y>0.5?texture2DLod(s_njinMetalRoughMap,uv,0.0):texture2D(s_njinMetalRoughMap,uv))
#define NJIN_AO_TEX(uv) (u_njinMipFilter[0].z>0.5?texture2DLod(s_njinOcclusionMap,uv,0.0):texture2D(s_njinOcclusionMap,uv))
#define NJIN_EMISSIVE_TEX(uv) (u_njinMipFilter[0].w>0.5?texture2DLod(s_njinEmissionMap,uv,0.0):texture2D(s_njinEmissionMap,uv))
uniform vec4 u_njinMapUv[10];
uniform vec4 u_njinMapFactors;
uniform vec4 u_njinMapEmission;
SAMPLER2D(s_njinEnvironmentBase,6);
SAMPLER2D(s_njinEnvironmentAtlas,7);
#define NJIN_ENV_BASE(uv) texture2D(s_njinEnvironmentBase,uv)
#define NJIN_ENV_ATLAS(uv) texture2D(s_njinEnvironmentAtlas,uv)
uniform vec4 u_njinEnvSH[9],u_njinEnvParams,u_njinEnvMode;
#define NJIN_ATAN2(y,x) atan2(y,x)
SAMPLER2D(s_njinPostAux,8);
// This program binds point-sampled depth as its primary image and linear HDR
// color as its auxiliary image. Keep these bindings local to the volume path.
#define NJIN_POST_COLOR(uv) texture2DLod(s_njinPostAux,uv,0.0)
#define NJIN_POST_AUX(uv) texture2D(s_njinColor,uv)
SAMPLER2D(s_njinPostSecond,9);
#define NJIN_POST_SECOND(uv) texture2DLod(s_njinPostSecond,uv,0.0)
uniform vec4 u_njinPost,u_njinPostExtra,u_njinPostConfig,u_njinPostSky,u_njinPostSun,u_njinMask;
uniform mat4 u_njinView;
uniform mat4 u_njinPostProjection;
uniform mat4 u_njinPostInverse;
uniform mat4 u_njinPreviousClip,u_njinPreviousView;
uniform vec4 u_njinTemporal;
SAMPLER2D(s_njinOpaqueDepth,10);
#define NJIN_OPAQUE_DEPTH(uv) texture2DLod(s_njinOpaqueDepth,uv,0.0)
uniform vec4 u_njinTransparency;
uniform mat4 u_njinTransparencyVP;
SAMPLER2D(s_njinVelocity,11);
#define NJIN_VELOCITY(uv) texture2DLod(s_njinVelocity,uv,0.0)
#include "njin_clusters.sh"
#include "njin_surface.sh"
#include "njin_world_fx.sh"
#include "njin_particles.sh"
void main() {gl_FragColor=vec4(njinVolume(v_texcoord0),1.0);}
