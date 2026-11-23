// Header, light records and packed index lists share one point-sampled RGBA32F image.
// The slot is shared with temporal velocity; surface/volume and temporal passes are separate.
uniform vec4 u_njinClusters[4];
uniform mat4 u_njinClusterView;
uniform mat4 u_njinClusterVP;
vec4 njinClusterRead(float index) {
  float width=u_njinClusters[2].x;
  return NJIN_VELOCITY(vec2(mod(index,width)+0.5,floor(index/width)+0.5)/u_njinClusters[2].xy);
}
vec2 njinClusterRange(vec3 position) {
  if(u_njinClusters[3].z<0.5) return vec2(0.0,u_njinOutput.w);
  vec4 grid=u_njinClusters[0],depth=u_njinClusters[1];
  vec4 clip=mul(u_njinClusterVP,vec4(position,1.0));
  if(clip.w<=0.0) return vec2(0.0,0.0);
  vec2 pixel=(clip.xy/clip.w*0.5+0.5)*u_njinClusters[3].xy;
  vec2 tile=clamp(floor(pixel/grid.w),vec2(0.0,0.0),grid.xy-vec2(1.0,1.0));
  float z=-mul(u_njinClusterView,vec4(position,1.0)).z;
  float distance=depth.w>0.5?(z-depth.x)/(depth.y-depth.x):log(max(z,depth.x)/depth.x)/depth.z;
  float slice=clamp(floor(distance*grid.z),0.0,grid.z-1.0);
  return njinClusterRead((slice*grid.y+tile.y)*grid.x+tile.x).xy;
}
float njinClusterLightIndex(float index) {
  if(u_njinClusters[3].z<0.5) return index;
  vec4 packed=njinClusterRead(u_njinClusters[2].w+floor(index/4.0));float channel=mod(index,4.0);
  return channel<0.5?packed.x:channel<1.5?packed.y:channel<2.5?packed.z:packed.w;
}
vec4 njinClusterLight(float index,int row) {
  if(u_njinClusters[3].z<0.5) return u_njinLocal[int(index)*4+row];
  return njinClusterRead(u_njinClusters[2].z+index*4.0+float(row));
}
