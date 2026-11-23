$input v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color, v_lightmap_uv
#include <bgfx_shader.sh>
SAMPLER2D(s_njinColor,0);
SAMPLER2D(s_njinNormalMap,2);
SAMPLER2D(s_njinMetalRoughMap,3);
SAMPLER2D(s_njinOcclusionMap,4);
SAMPLER2D(s_njinEmissionMap,5);
SAMPLER2D(s_njinPostAux,8);
#define NJIN_POST_COLOR(uv) texture2DLod(s_njinColor,uv,0.0)
#define NJIN_NORMAL_TEX(uv) texture2DLod(s_njinNormalMap,uv,0.0)
#define NJIN_MR_TEX(uv) texture2DLod(s_njinMetalRoughMap,uv,0.0)
#define NJIN_AO_TEX(uv) texture2DLod(s_njinOcclusionMap,uv,0.0)
#define NJIN_EMISSIVE_TEX(uv) texture2DLod(s_njinEmissionMap,uv,0.0)
uniform vec4 u_njinPost;
uniform vec4 u_njinPostExtra;
uniform vec4 u_njinLegacyPost[10];
uniform vec4 u_njinLightEdges[64];
uniform vec4 u_njinMapUv[10];
// Same color convention as njin_surface.sh, without its 3D/post pipelines.
vec3 njinSplat(float v) {return vec3(v,v,v);}
vec3 njinLinear(vec3 c) {return mix(c/12.92,pow(max((c+0.055)/1.055,njinSplat(0.0)),njinSplat(2.4)),step(njinSplat(0.04045),c));}
#include "njin_light2d.sh"
void main() {gl_FragColor=vec4(njinLight2D(v_texcoord0),1.0);}
