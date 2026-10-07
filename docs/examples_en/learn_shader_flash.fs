#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 mouse;   // mouse.x: flash amount, 0 is normal, 1 is fully white
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;
  // Blend the pixel's color toward white, keeping its opacity: transparent parts stay transparent,
  // so the sprite lights up exactly along its own shape.
  finalColor = vec4(mix(c.rgb, vec3(1.0), mouse.x), c.a);
}
