#version 330

// Places one person's quad, drawn with draw_instanced(): the quad is the unit
// square in vertexPosition, and the three instance attributes are what
// draw.cpp wrote for this person (see `instance` in figure.h):
//
//   instance0 = centre.x, centre.y, side length, rotation (degrees, clockwise)
//   instance1 = pose or sheet frame, colour index, flip (-1 faces left), mode
//   instance2 = four numbers person.fs reads according to the mode
//
// Everything about how to draw the person is person.fs's business; this only
// mirrors the quad when they face left and passes the rest on.

in vec3 vertexPosition;
in vec4 instance0;
in vec4 instance1;
in vec4 instance2;
uniform mat4 mvp;

out vec2 v_local;       // 0..1 across the quad, mirrored when facing left
flat out vec4 v_data0;  // instance1
flat out vec4 v_data1;  // instance2

void main() {
  vec2 corner = vertexPosition.xy;
  v_local = vec2(instance1.z < 0.0 ? 1.0 - corner.x : corner.x, corner.y);
  v_data0 = instance1;
  v_data1 = instance2;

  float r = radians(instance0.w);
  vec2 c = (corner - 0.5) * instance0.z;
  // Clockwise on screen, y down: the same turn texture_draw_ex gives.
  vec2 world = instance0.xy + vec2(c.x * cos(r) - c.y * sin(r), c.x * sin(r) + c.y * cos(r));
  gl_Position = mvp * vec4(world, 0.0, 1.0);
}
