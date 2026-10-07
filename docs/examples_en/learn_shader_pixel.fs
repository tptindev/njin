#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 mouse;   // mouse.x: cell size, from 1 to 20 pixels
out vec4 finalColor;

void main() {
  float size = 1.0 + floor(mouse.x * 19.0);          // pixelation cell size, in screen pixels
  vec2 cells = resolution / size;                    // number of cells along each axis

  // Round the coordinate down to its cell, then sample at the cell's CENTER: every pixel in a cell sees the same color.
  vec2 uv = (floor(fragTexCoord * cells) + 0.5) / cells;
  finalColor = vec4(texture(texture0, uv).rgb, 1.0);
}
