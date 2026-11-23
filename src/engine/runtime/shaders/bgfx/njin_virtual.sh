// Njin software virtual albedo. The table and physical pages share sampler 0.
// A table entry stores cell X/Y and the actual resident ancestor mip in RGB8.
uniform vec4 u_njinVirtual[2];
float njinVirtualWrap(float value,float mode) {
  if(mode>1.5) return 1.0-abs(fract(value*0.5)*2.0-1.0);
  if(mode>0.5) return clamp(value,0.0,1.0);
  return fract(value);
}
vec4 njinVirtualLevel(vec2 original,float requested) {
  vec4 info=u_njinVirtual[0],atlas=u_njinVirtual[1];
  float wrapS=mod(atlas.w,4.0),wrapT=floor(atlas.w/4.0);
  vec2 uv=min(vec2(njinVirtualWrap(original.x,wrapS),njinVirtualWrap(original.y,wrapT)),vec2(0.99999994,0.99999994));
  float grid=max(1.0,exp2(atlas.z-requested)/info.y);
  float offset=0.0;
  for(int i=0;i<15;++i) {
    if(float(i)<requested) {float g=max(1.0,exp2(atlas.z-float(i))/info.y);offset+=g*g;}
  }
  vec2 tile=floor(uv*grid);
  float index=offset+tile.y*grid+tile.x;
  vec2 tableUv=(vec2(mod(index,atlas.x),floor(index/atlas.x))+vec2(0.5,0.5))/atlas.xy;
  vec3 mapping=floor(texture2DLod(s_njinColor,tableUv,0.0).rgb*255.0+vec3(0.5,0.5,0.5));
  vec2 virtualPixel=uv*max(1.0,exp2(atlas.z-mapping.z));
  float extent=max(1.0,exp2(atlas.z-mapping.z));
  if(wrapS>0.5) virtualPixel.x=clamp(virtualPixel.x,0.5,extent-0.5);
  if(wrapT>0.5) virtualPixel.y=clamp(virtualPixel.y,0.5,extent-0.5);
  vec2 local=mod(virtualPixel,vec2(info.y,info.y));
  vec2 physical=(mapping.xy*(info.y+2.0)+vec2(1.0,info.w+1.0)+local)/atlas.xy;
  return texture2DLod(s_njinColor,physical,0.0);
}
vec4 njinVirtualColor(vec2 uv) {
  vec2 dx=dFdx(uv)*u_njinVirtual[0].x,dy=dFdy(uv)*u_njinVirtual[0].x;
  float level=clamp(0.5*log2(max(max(dot(dx,dx),dot(dy,dy)),0.000001)),0.0,u_njinVirtual[1].z);
  float coarse=floor(level);
  return mix(njinVirtualLevel(uv,coarse),njinVirtualLevel(uv,min(coarse+1.0,u_njinVirtual[1].z)),fract(level));
}
