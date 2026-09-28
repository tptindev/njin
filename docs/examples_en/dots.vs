#version 330

// vertexPosition: corner of the unit square, (0, 0) to (1, 1).
// instance0, instance1: data of the dot being drawn.
in vec3 vertexPosition;
in vec4 instance0; // center x, center y, side length, (unused)
in vec4 instance1; // color
uniform mat4 mvp;

out vec2 uv;
out vec4 color;

void main() {
  uv = vertexPosition.xy;
  color = instance1;
  vec2 world = instance0.xy + (vertexPosition.xy - 0.5) * instance0.z;
  gl_Position = mvp * vec4(world, 0.0, 1.0);
}
