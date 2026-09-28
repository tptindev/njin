#version 330

// Bakes one cell of a sheet per instance (see sheet.cpp, sheet_bake()).
in vec3 vertexPosition;
in vec4 instance0; // cell centre x, y (sheet pixels), frame, frame count
in vec4 instance1; // base direction (0 S .. 4 N), pose, build, hair style
uniform mat4 mvp;
uniform vec2 u_cell; // cell size, pixels

out vec2 v_local; // 0..1 across the cell, y down
flat out vec2 v_frame;
flat out vec4 v_pose;

void main() {
  v_local = vertexPosition.xy;
  v_frame = instance0.zw;
  v_pose = instance1;
  vec2 px = instance0.xy + (vertexPosition.xy - 0.5) * u_cell;
  gl_Position = mvp * vec4(px, 0.0, 1.0);
}
