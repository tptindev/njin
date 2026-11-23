
float njinParticleHash(vec3 p) {
  p = fract(p * 0.3183099 + 0.1);
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float njinParticleNoise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(njinParticleHash(i + vec3(0, 0, 0)), njinParticleHash(i + vec3(1, 0, 0)), f.x),
                 mix(njinParticleHash(i + vec3(0, 1, 0)), njinParticleHash(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(njinParticleHash(i + vec3(0, 0, 1)), njinParticleHash(i + vec3(1, 0, 1)), f.x),
                 mix(njinParticleHash(i + vec3(0, 1, 1)), njinParticleHash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float njinParticleFbm(vec3 p, float pix) {
  float v = 0.0, a = 0.5;
  for (int i = 0; i < 4; i++) {
    v += a * mix(njinParticleNoise(p), 0.5, smoothstep(0.25, 0.5, pix));
    p = p * 2.03 + vec3(1.7, 9.2, 3.1);
    a *= 0.5;
    pix *= 2.03;
  }
  return v;
}

vec4 njinParticle_smoke(vec2 uv,vec3 position,vec4 color,float time,vec3 lightDir,vec3 lightColor,vec3 ambient,float intensity) {

  vec2 c = uv * 2.0 - 1.0;
  float r = dot(c, c);
  if (r >= 1.0) discard;
  vec3 n = vec3(c.x, -c.y, sqrt(1.0 - r));
  // The quad's side in world units, from how fast world and image coordinates
  // change across a pixel. Small puffs keep the noise at its world scale; a
  // big one gets at most a few cells across, so it stays a soft cloud.
  float side = length(dFdx(position)) / max(length(dFdx(uv)), 1e-6);
  float freq = min(2.2, 3.0 / max(side, 1e-3));
  vec3 p = position * freq + vec3(0.0, -time * 0.9, time * 0.25);
  float d = njinParticleFbm(p + vec3(c * 0.6, 0.0), length(fwidth(position)) * freq);
  float age = 1.0 - color.a;
  float body = smoothstep(0.0, 0.55, 1.0 - r) * smoothstep(0.18 + 0.35 * age, 0.75, d + 0.25 * (1.0 - r));
  if (body <= 0.003) discard;
  float lit = clamp(dot(n, normalize(vec3(lightDir.x * 0.4, max(lightDir.y, 0.3), 0.6))), 0.0, 1.0);
  lit = mix(lit, 1.0, 0.25) * mix(0.75, 1.1, d);
  vec3 shade = clamp(ambient * 1.4, 0.0, 1.0);
  vec3 sun = clamp(lightColor * 0.4 + 0.6, 0.0, 1.2);
  return vec4(color.rgb * mix(shade, sun, lit), body * min(color.a * 1.6, 1.0));
}

vec4 njinParticle_fire(vec2 uv,vec3 position,vec4 color,float time,vec3 lightDir,vec3 lightColor,vec3 ambient,float intensity) {

  vec2 c = uv * 2.0 - 1.0;
  float r = length(c);
  if (r >= 1.0) discard;
  float flicker = njinParticleNoise(position * 7.0 + vec3(0.0, time * 9.0, time * 3.0));
  float heat = clamp((1.0 - r) * (0.6 + 0.8 * flicker) * (0.4 + color.a), 0.0, 1.0);
  vec3 colour = mix(vec3(0.6, 0.08, 0.02), vec3(1.0, 0.55, 0.12), smoothstep(0.1, 0.5, heat));
  colour = mix(colour, vec3(1.0, 0.95, 0.75), smoothstep(0.55, 0.95, heat));
  return vec4(colour, smoothstep(0.0, 0.35, heat) * color.a * 0.45);
}

vec4 njinParticle_spark(vec2 uv,vec3 position,vec4 color,float time,vec3 lightDir,vec3 lightColor,vec3 ambient,float intensity) {

  float across = abs(uv.x * 2.0 - 1.0);
  float core = exp(-across * across * 18.0);
  float glow = exp(-across * across * 3.0);
  float tail = pow(1.0 - uv.y, 1.6);
  float cool = 1.0 - color.a;
  vec3 hot = mix(vec3(1.0, 0.95, 0.8), vec3(1.0, 0.55, 0.15), cool);
  vec3 edge = mix(vec3(1.0, 0.45, 0.08), vec3(0.7, 0.12, 0.02), cool);
  vec3 colour = (hot * core + edge * glow * 0.6) * tail;
  return vec4(colour, clamp(color.a * (glow + core) * tail, 0.0, 1.0));
}

vec4 njinParticle_flame(vec2 uv,vec3 position,vec4 color,float time,vec3 lightDir,vec3 lightColor,vec3 ambient,float intensity) {

  float thrust = 0.4 + 0.6 * sqrt(color.r);
  float age = 1.0 - color.a;
  float across = abs(uv.x * 2.0 - 1.0);
  float body = exp(-across * across * 3.0) * smoothstep(0.0, 0.15, uv.y) * smoothstep(1.0, 0.75, uv.y);
  float band = pow(0.5 + 0.5 * cos(age * 6.2831853 * 4.0), 6.0) * smoothstep(0.75, 0.1, age);
  float tear = njinParticleNoise(position * 6.0 + vec3(0.0, -time * 14.0, time * 5.0));
  float heat = (1.0 - age) * (0.85 + 0.3 * tear);
  vec3 colour = mix(vec3(0.55, 0.08, 0.02), vec3(1.0, 0.42, 0.08), smoothstep(0.05, 0.35, heat));
  colour = mix(colour, vec3(1.0, 0.82, 0.45), smoothstep(0.35, 0.7, heat));
  colour = mix(colour, vec3(0.78, 0.86, 1.0), smoothstep(0.75, 0.97, heat));
  float light = body * thrust * (0.35 + 0.65 * pow(1.0 - age, 0.7)) * (0.8 + 0.9 * band) * (0.7 + 0.6 * tear);
  return vec4(colour, clamp(light * 0.45, 0.0, 1.0));
}

vec4 njinParticle_ribbon_flat(vec2 uv,vec3 position,vec4 color,float time,vec3 lightDir,vec3 lightColor,vec3 ambient,float intensity) {

  float x = abs(uv.x * 2.0 - 1.0);
  return vec4(color.rgb * intensity, color.a * smoothstep(1.0, 0.75, x));
}

vec4 njinParticle_ribbon_glow(vec2 uv,vec3 position,vec4 color,float time,vec3 lightDir,vec3 lightColor,vec3 ambient,float intensity) {

  float x = uv.x * 2.0 - 1.0;
  float halo = max(exp(-4.5 * x * x) - 0.011, 0.0) / 0.989;
  float core = exp(-60.0 * x * x);
  float hot = core * clamp(color.a * 1.5, 0.0, 1.0);
  vec3 colour = mix(color.rgb, vec3(1.0, 0.98, 0.94), hot) * intensity;
  return vec4(colour, clamp(halo * 0.55 + core, 0.0, 1.0) * color.a);
}

vec4 njinParticle(vec2 uv,vec3 position,vec4 color) {
  float style=u_njinParticleFx.x,time=u_njinParticleFx.y,intensity=u_njinParticleFx.w;
  vec3 dir=-normalize(u_njinLight[0].xyz),sun=u_njinLight[1].rgb,ambient=u_njinLight[2].rgb;

  if(style>1.5 && style<2.5) return njinParticle_smoke(uv,position,color,time,dir,sun,ambient,intensity);

  if(style>2.5 && style<3.5) return njinParticle_fire(uv,position,color,time,dir,sun,ambient,intensity);

  if(style>3.5 && style<4.5) return njinParticle_spark(uv,position,color,time,dir,sun,ambient,intensity);

  if(style>4.5 && style<5.5) return njinParticle_flame(uv,position,color,time,dir,sun,ambient,intensity);

  if(style>5.5 && style<6.5) return njinParticle_ribbon_flat(uv,position,color,time,dir,sun,ambient,intensity);

  if(style>6.5 && style<7.5) return njinParticle_ribbon_glow(uv,position,color,time,dir,sun,ambient,intensity);

  float r=length(uv*2.0-1.0);
  if(u_njinParticleFx.z<0.5) color.a*=clamp((1.0-r)*32.0,0.0,1.0);
  if(u_njinParticleFx.z>1.5) color.a*=exp(-r*r*4.5)*smoothstep(1.0,0.85,r);
  return color;
}
