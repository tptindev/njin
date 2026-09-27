#version 330

in vec3 vertexPosition;
in vec4 vertexColor;

uniform mat4 mvp;

// Vertices arrive in world coordinates and the camera lives in mvp, so the
// fragment shader gets the world position of its own pixel for free and needs
// to know nothing about the camera, the window or the virtual resolution.
out vec2 worldPos;

void main() {
  worldPos = vertexPosition.xy;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
