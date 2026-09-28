#version 330

// The demo's whole-frame pass (camera_set_post_shader): night with lights (7),
// a colour ramp for dusk (8) and haze (9). Each is off at 0, so with all three
// off the frame comes out as it went in.

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0; // the frame the game drew
uniform vec4 colDiffuse;

uniform sampler2D ramp;  // 256 x 1: what brightness turns into at dusk
uniform sampler2D noise; // tiling grey noise

uniform float time;
uniform vec2 resolution; // logical screen size, in pixels
uniform vec3 ambient; // the light everywhere at night, before any lamp
uniform float night;
uniform float dusk;
uniform float haze;

const int max_lights = 8;
uniform int light_count;
uniform vec4 lights[max_lights];       // xy: position in pixels, z: radius in pixels, w: strength
uniform vec4 light_colors[max_lights]; // rgb: colour

out vec4 finalColor;

void main() {
  // The frame is stored upside down: v is 1 at the top of the screen.
  vec2 uv = fragTexCoord;
  vec2 pixel = vec2(uv.x, 1.0 - uv.y) * resolution;

  // Haze: two slow drifting noise samples nudge where the frame is read from.
  vec2 spot = uv * vec2(resolution.x / resolution.y, 1.0) * 3.0;
  vec2 nudge = vec2(texture(noise, spot + vec2(time * 0.04, time * 0.02)).r,
                    texture(noise, spot + vec2(0.37 - time * 0.03, time * 0.05)).r) - 0.5;
  vec3 c = texture(texture0, uv + nudge * haze * 0.03).rgb;

  // Dusk: look the brightness up in the ramp.
  float lum = dot(c, vec3(0.299, 0.587, 0.114));
  c = mix(c, texture(ramp, vec2(lum, 0.5)).rgb, dusk);

  // Night: a dim blue everywhere, plus each light's pool.
  vec3 lit = ambient;
  for (int i = 0; i < light_count; i++) {
    float d = clamp(1.0 - length(pixel - lights[i].xy) / lights[i].z, 0.0, 1.0);
    lit += light_colors[i].rgb * lights[i].w * d * d;
  }
  c *= mix(vec3(1.0), lit, night);

  finalColor = vec4(c, 1.0);
}
