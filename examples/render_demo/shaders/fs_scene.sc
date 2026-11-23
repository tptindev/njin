$input v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color
// assets/scene.fs for the SDL3/bgfx renderer (a .njshader package, see
// docs/renderer-shaders.md): night with lights, a colour ramp for dusk and haze.
// Here v is 0 at the top of the screen; the OpenGL frame is stored upside down.
#include <bgfx_shader.sh>
SAMPLER2D(s_demoFrame, 0); // the frame the game drew
SAMPLER2D(s_demoRamp, 1);  // 256 x 1: what brightness turns into at dusk
SAMPLER2D(s_demoNoise, 2); // tiling grey noise
uniform vec4 u_demoTime;
uniform vec4 u_demoResolution; // logical screen size, in pixels
uniform vec4 u_demoAmbient;    // the light everywhere at night, before any lamp
uniform vec4 u_demoNight;
uniform vec4 u_demoDusk;
uniform vec4 u_demoHaze;
uniform vec4 u_demoLightCount; // an int: low 16 bits in x
uniform vec4 u_demoLights[8];      // xy: position in pixels, z: radius in pixels, w: strength
uniform vec4 u_demoLightColors[8]; // rgb: colour
void main() {
  vec2 uv = v_texcoord0;
  vec2 resolution = u_demoResolution.xy;
  vec2 pixel = uv * resolution;
  float time = u_demoTime.x, haze = u_demoHaze.x;

  // Haze: two slow drifting noise samples nudge where the frame is read from.
  vec2 spot = fract(uv * vec2(resolution.x / resolution.y, 1.0) * 3.0);
  vec2 nudge = vec2(texture2D(s_demoNoise, fract(spot + vec2(time * 0.04, time * 0.02))).r,
                    texture2D(s_demoNoise, fract(spot + vec2(0.37 - time * 0.03, time * 0.05))).r) - 0.5;
  vec3 c = texture2D(s_demoFrame, uv + nudge * haze * 0.03).rgb;

  // Dusk: look the brightness up in the ramp.
  float lum = dot(c, vec3(0.299, 0.587, 0.114));
  c = mix(c, texture2D(s_demoRamp, vec2(lum, 0.5)).rgb, u_demoDusk.x);

  // Night: a dim blue everywhere, plus each light's pool.
  vec3 lit = u_demoAmbient.xyz;
  int count = int(u_demoLightCount.x);
  for (int i = 0; i < 8; i++) {
    if (i >= count) break;
    float d = clamp(1.0 - length(pixel - u_demoLights[i].xy) / u_demoLights[i].z, 0.0, 1.0);
    lit += u_demoLightColors[i].rgb * u_demoLights[i].w * d * d;
  }
  c *= mix(vec3(1.0, 1.0, 1.0), lit, u_demoNight.x);

  gl_FragColor = vec4(c, 1.0);
}
