#version 330

// vertexPosition: góc hình vuông đơn vị, (0, 0) đến (1, 1).
// instance0, instance1: dữ liệu của chấm đang vẽ.
in vec3 vertexPosition;
in vec4 instance0; // tâm x, tâm y, cạnh, (bỏ trống)
in vec4 instance1; // màu
uniform mat4 mvp;

out vec2 uv;
out vec4 color;

void main() {
  uv = vertexPosition.xy;
  color = instance1;
  vec2 world = instance0.xy + (vertexPosition.xy - 0.5) * instance0.z;
  gl_Position = mvp * vec4(world, 0.0, 1.0);
}
