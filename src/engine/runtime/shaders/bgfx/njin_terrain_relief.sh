// Relief Njin: lưới mịn thay tessellation; cùng displacement cho màu, depth và bóng.
// The fragment stage takes all 16 samplers, and a program's stages share them: the relief
// fields use stages 13-15 under the names of the lightmap and under maps
// (njin_advanced_material.sh), which a relief draw never has (njin_bgfx.cpp binds them).
SAMPLER2D(s_njinLightmap,13);
SAMPLER2D(s_njinUnderMap,14);
SAMPLER2D(s_njinUnderNormal,15);
#define s_njinReliefGround s_njinLightmap
#define s_njinReliefSplat s_njinUnderMap
#define s_njinReliefLayers s_njinUnderNormal
uniform vec4 u_njinTerrainRelief[6];
vec4 njinReliefField(sampler2D map,vec2 cell) {
  float size=u_njinTerrainRelief[0].w;
  cell=clamp(cell,vec2(0.0,0.0),vec2(size-1.0,size-1.0));
  vec2 i=floor(cell),f=fract(cell);
  vec2 a=(i+vec2(0.5,0.5))/size,b=min(i+vec2(1.5,1.5),vec2(size-0.5,size-0.5))/size;
  return mix(mix(texture2DLod(map,a,0.0),texture2DLod(map,vec2(b.x,a.y),0.0),f.x),
             mix(texture2DLod(map,vec2(a.x,b.y),0.0),texture2DLod(map,b,0.0),f.x),f.y);
}
float njinReliefLayer(int layer,vec3 p,vec3 weights) {
  float tile=u_njinTerrainRelief[3][layer];
  float lod=max(log2(max(u_njinTerrainRelief[4].z*u_njinTerrainRelief[0].z*u_njinTerrainRelief[4].x/tile,1.0)),0.0);
  if(weights.y>0.97)
    return mix(texture2DLod(s_njinReliefLayers,p.xz/tile,lod)[layer],
               texture2DLod(s_njinReliefLayers,p.xz/(tile*3.7)+vec2(0.31,0.31),lod)[layer],0.3);
  return texture2DLod(s_njinReliefLayers,p.zy/tile,lod)[layer]*weights.x+
         texture2DLod(s_njinReliefLayers,p.xz/tile,lod)[layer]*weights.y+
         texture2DLod(s_njinReliefLayers,p.xy/tile,lod)[layer]*weights.z;
}
vec3 njinReliefPoint(vec2 cell,vec4 eye,out vec3 base,out vec3 normal,out vec4 splat) {
  vec4 grid=u_njinTerrainRelief[0];
  cell=clamp(cell,vec2(0.0,0.0),vec2(grid.w-1.0,grid.w-1.0));
  vec4 field=njinReliefField(s_njinReliefGround,cell);
  normal=normalize(field.xyz);
  base=vec3(grid.x+cell.x*grid.z,field.w,grid.y+cell.y*grid.z);
  splat=njinReliefField(s_njinReliefSplat,cell);
  vec4 w=pow(max(splat,vec4(0.0,0.0,0.0,0.0)),vec4(1.6,1.6,1.6,1.6));
  float sum=dot(w,vec4(1.0,1.0,1.0,1.0));w=sum>0.001?w/sum:vec4(1.0,0.0,0.0,0.0);
  vec3 blend=pow(abs(normal),vec3(4.0,4.0,4.0));blend/=max(dot(blend,vec3(1.0,1.0,1.0)),0.001);
  float displacement=0.0;
  for(int l=0;l<4;++l) if(float(l)<u_njinTerrainRelief[4].w&&u_njinTerrainRelief[2][l]>0.0&&w[l]>0.004)
    displacement+=(njinReliefLayer(l,base,blend)-0.5)*u_njinTerrainRelief[2][l]*w[l];
  float fade=1.0-smoothstep(eye.w*0.7,eye.w,length(base-eye.xyz));
  return base+normal*displacement*fade;
}
vec3 njinReliefVertex(vec3 position,vec4 eye,out vec3 base,out vec3 normal,out vec4 splat) {
  vec4 grid=u_njinTerrainRelief[0];
  return njinReliefPoint((position.xz-grid.xy)/grid.z,eye,base,normal,splat);
}
