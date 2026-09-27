#version 330

// Hands the paper shader the world position of each fragment. rlgl's batch
// holds vertices in world units and the camera lives in `mvp`, so the grain
// stays glued to the sheet when the camera pans and zooms.

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 world_pos;

void main() {
  world_pos = vertexPosition.xy;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
