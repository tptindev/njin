#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float time;
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;

  // fract(time * 4.0) goes from 0 to 1 four times per second; step(0.5, x) is 0 when x < 0.5 and 1 from 0.5 on.
  // The result is a signal 0, 1, 0, 1... four beats per second, with no if.
  float on = step(0.5, fract(time * 4.0));
  finalColor = vec4(mix(c.rgb, vec3(1.0), on), c.a);
}
