#version 330

in vec2 fragTexCoord;
uniform float time;   // seconds since start, passed in by the program every frame
out vec4 finalColor;

void main() {
  // cos gives values from -1 to 1; multiplying by 0.5 and adding 0.5 brings them to 0..1.
  // The three channels are out of phase (0, 2, 4), so the color runs through many hues and changes with both position and time.
  vec3 col = 0.5 + 0.5 * cos(time + fragTexCoord.xyx * 3.0 + vec3(0.0, 2.0, 4.0));
  finalColor = vec4(col, 1.0);
}
