#version 330

// The pass over the whole world picture (camera_set_post_shader), not the HUD:
// - invisible shockwaves: nothing is drawn, the picture behind each wave
//   front is bent outward from its centre, so a blast shows only as a ripple
//   running through the grass and the men;
// - light: the time of day tints everything, and at dusk and at night torches,
//   camp fires and blasts light the ground round them, in a few hard steps as
//   pixel art does.

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0; // the world as the game drew it
uniform vec4 colDiffuse;

uniform vec2 resolution; // screen size, pixels

const int max_waves = 16;
uniform int wave_count;
// xy: centre, pixels from the top left; z: radius of the front, pixels;
// w: how far the front shifts the picture, pixels.
uniform vec4 waves[max_waves];

uniform vec3 ambient; // light everywhere: white by day, blue at night, orange at dusk
const int max_lights = 64;
uniform int light_count;
// xy: centre, pixels from the top left; z: radius, pixels; w: strength.
uniform vec4 lights[max_lights];
const vec3 flame = vec3(1.0, 0.72, 0.38);

out vec4 finalColor;

void main() {
  // The frame is stored upside down: v is 1 at the top of the screen.
  vec2 pixel = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y) * resolution;
  vec2 shift = vec2(0.0);
  for (int i = 0; i < wave_count; ++i) {
    vec2 to = pixel - waves[i].xy;
    float d = length(to);
    float width = 5.0 + waves[i].z * 0.2;
    float x = (d - waves[i].z) / width; // -1 just behind the front, 1 just ahead
    if (abs(x) < 1.0 && d > 0.001) {
      // One swell: the picture is pulled toward the front from both sides,
      // so the front itself looks like a ridge in the ground.
      float swell = sin(x * 3.14159265) * (1.0 - abs(x));
      shift += (to / d) * swell * waves[i].w;
    }
  }
  vec2 from = pixel - shift;
  vec2 uv = vec2(from.x, resolution.y - from.y) / resolution;
  vec4 scene = texture(texture0, uv) * colDiffuse * fragColor;

  // Light, measured on the whole pixel so it falls in blocks.
  vec2 cell = floor(pixel) + 0.5;
  float glow = 0.0;
  for (int i = 0; i < light_count; ++i) {
    float d = length(cell - lights[i].xy) / lights[i].z;
    if (d < 1.0)
      glow += lights[i].w * (1.0 - d * d);
  }
  glow = floor(min(glow, 1.0) * 4.0 + 0.5) / 4.0; // four steps, like hand-drawn light
  vec3 lit = max(ambient, flame * glow + ambient * (1.0 - glow * 0.5));
  finalColor = vec4(scene.rgb * lit, scene.a);
}
