#version 330

// These names are dictated by raylib; if you get them wrong, the shader gets no data.
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Your own uniform.
uniform float amount;

out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
  float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));
  finalColor = vec4(mix(c.rgb, vec3(g), amount), c.a);
}
