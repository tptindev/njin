$input v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color, v_lightmap_uv
#include <bgfx_shader.sh>
SAMPLER2D(s_njinColor,0);
SAMPLER2D(s_njinPostAux,8);
SAMPLER2D(s_njinPostSecond,9);
uniform vec4 u_njinPost;
#define SMAA_RT_METRICS vec4(u_njinPost.yz,vec2(1.0,1.0)/u_njinPost.yz)
#define SMAA_PRESET_HIGH 1
#define SMAA_CUSTOM_SL 1
#if BGFX_SHADER_LANGUAGE_HLSL
#define SMAATexture2D(tex) BgfxSampler2D tex
#else
#define SMAATexture2D(tex) sampler2D tex
#endif
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) texture2DLod(tex,coord,0.0)
#define SMAASampleLevelZeroPoint(tex, coord) texture2DLod(tex,coord,0.0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) texture2DLod(tex,(coord)+vec2(offset)*SMAA_RT_METRICS.xy,0.0)
#define SMAASample(tex, coord) texture2DLod(tex,coord,0.0)
#define SMAASamplePoint(tex, coord) texture2DLod(tex,coord,0.0)
#define SMAASampleOffset(tex, coord, offset) texture2DLod(tex,(coord)+vec2(offset)*SMAA_RT_METRICS.xy,0.0)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define lerp(a,b,t) mix(a,b,t)
#define saturate(a) clamp(a,0.0,1.0)
#define mad(a,b,c) ((a)*(b)+(c))
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4
#include <SMAA.hlsl>
void main() {
  vec2 uv=v_texcoord0;
#if NJIN_SMAA_STAGE == 0
  vec4 offsets[3];SMAAEdgeDetectionVS(uv,offsets);
  gl_FragColor=vec4(SMAALumaEdgeDetectionPS(uv,offsets,s_njinColor),0.0,1.0);
#elif NJIN_SMAA_STAGE == 1
  vec2 pixel;vec4 offsets[3];SMAABlendingWeightCalculationVS(uv,pixel,offsets);
  gl_FragColor=SMAABlendingWeightCalculationPS(uv,pixel,offsets,s_njinColor,s_njinPostAux,s_njinPostSecond,vec4(0.0,0.0,0.0,0.0));
#else
  vec4 offset;SMAANeighborhoodBlendingVS(uv,offset);
  gl_FragColor=vec4(SMAANeighborhoodBlendingPS(uv,offset,s_njinColor,s_njinPostAux).rgb,1.0);
#endif
}
