// Color formulas adapted from modules/post_fx.cpp.
float njinLegacyHash(vec2 p) {return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}
vec3 njinLegacyPost(vec2 coord) {
  float brightness=u_njinLegacyPost[0].x,contrast=u_njinLegacyPost[0].y,saturation=u_njinLegacyPost[0].z,sepia=u_njinLegacyPost[0].w;
  vec4 tint=u_njinLegacyPost[1],vignetteColor=u_njinLegacyPost[3];
  float vignette=u_njinLegacyPost[2].x,vignetteRadius=u_njinLegacyPost[2].y,vignetteSoftness=u_njinLegacyPost[2].z,bloom=u_njinLegacyPost[2].w;
  float chromatic=u_njinLegacyPost[4].x,scanlines=u_njinLegacyPost[4].y,scanlineSize=u_njinLegacyPost[4].z,curve=u_njinLegacyPost[4].w;
  float pixelate=u_njinLegacyPost[5].x,grain=u_njinLegacyPost[5].y,time=u_njinLegacyPost[5].z;
  vec2 resolution=1.0/u_njinPost.yz;

  vec2 uv = coord;
  if (curve > 0.0) {
    vec2 cc = uv * 2.0 - 1.0;
    cc *= 1.0 + curve * (cc.yx * cc.yx);
    uv = cc * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
      return vec3(0.0,0.0,0.0);
    }
  }
  if (pixelate >= 2.0) {
    vec2 cell = pixelate / resolution;
    uv = (floor(uv / cell) + 0.5) * cell;
  }

  vec3 col;
  if (chromatic > 0.0) {
    vec2 shift = (uv - 0.5) * 2.0 * chromatic / resolution;
    col.r = NJIN_POST_COLOR( uv + shift).r;
    col.g = NJIN_POST_COLOR( uv).g;
    col.b = NJIN_POST_COLOR( uv - shift).b;
  } else {
    col = NJIN_POST_COLOR( uv).rgb;
  }
  if (bloom > 0.0)
    col += NJIN_POST_AUX( uv).rgb * bloom;

  col += brightness;
  col = (col - 0.5) * contrast + 0.5;
  float l = dot(col, vec3(0.299, 0.587, 0.114));
  col = mix(njinSplat(l), col, saturation);
  if (sepia > 0.0) {
    vec3 s = vec3(dot(col, vec3(0.393, 0.769, 0.189)),
                  dot(col, vec3(0.349, 0.686, 0.168)),
                  dot(col, vec3(0.272, 0.534, 0.131)));
    col = mix(col, s, sepia);
  }
  col *= tint.rgb;

  if (scanlines > 0.0) {
    float s = 0.5 + 0.5 * cos(coord.y * resolution.y * 6.2831853 / scanlineSize);
    col *= 1.0 - scanlines * s;
  }
  if (vignette > 0.0) {
    vec2 d = (uv - 0.5) * vec2(resolution.x / resolution.y, 1.0);
    float r = length(d) / length(vec2(resolution.x / resolution.y, 1.0) * 0.5);
    float v = smoothstep(vignetteRadius, vignetteRadius + vignetteSoftness, r);
    col = mix(col, vignetteColor.rgb, v * vignette);
  }
  if (grain > 0.0)
    col += (njinLegacyHash(coord * resolution + fract(time) * 100.0) - 0.5) * grain;

  return clamp(col,0.0,1.0);
}

vec3 njinLegacyBlur(vec2 uv) {
  vec2 d=u_njinLegacyPost[9].xy*u_njinPost.yz;
  return NJIN_POST_COLOR(uv).rgb*0.227027027+
    (NJIN_POST_COLOR(uv+d*1.384615385).rgb+NJIN_POST_COLOR(uv-d*1.384615385).rgb)*0.316216216+
    (NJIN_POST_COLOR(uv+d*3.230769231).rgb+NJIN_POST_COLOR(uv-d*3.230769231).rgb)*0.070270270;
}
vec3 njinLegacyDof(vec2 uv) {
  float depth=NJIN_POST_SECOND(uv).r;
  float d=depth*2.0-1.0;
  // GPU depth textures use native 0..1 storage on every bgfx backend.
  vec4 focus=u_njinLegacyPost[6],center=u_njinLegacyPost[7],haze=u_njinLegacyPost[8];
  float k;
  if(center.w>0.0) {
    vec4 world=mul(u_njinPostInverse,vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,d,1.0));
    k=smoothstep(center.w,center.w+max(focus.z,0.0001),length(world.xyz/world.w-center.xyz));
  } else {
    float n=u_njinLegacyPost[9].x,f=u_njinLegacyPost[9].y;
    float z=n<0.0?-n+depth*(f+n):2.0*n*f/(f+n-d*(f-n));
    k=smoothstep(focus.y,focus.y+max(focus.z,0.0001),abs(z-focus.x));
  }
  return mix(mix(NJIN_POST_COLOR(uv).rgb,NJIN_POST_AUX(uv).rgb,k),haze.rgb,k*haze.a);
}
