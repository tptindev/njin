#version 330

// Places one person's quad, drawn with draw_instanced(): the quad is the unit
// square in vertexPosition, and the three instance attributes are what
// draw.cpp wrote for this person (see its `instance` struct):
//
//   instance0 = centre.x, centre.y, side length, rotation (degrees, clockwise)
//   instance1 = pose, cloth colour index, flip (-1 faces left), unused
//   instance2 = phase, extra, extra2, seed (a number of the person's own)
//
// person.fs reads the pose and the colour index back as whole numbers.

in vec3 vertexPosition;
in vec4 instance0;
in vec4 instance1;
in vec4 instance2;
uniform mat4 mvp;

out vec2 v_local;       // 0..1 across the quad, mirrored when facing left
flat out vec2 v_codes;  // pose, cloth colour index
flat out vec4 v_params; // phase, extra, extra2, seed

void main() {
  vec2 corner = vertexPosition.xy;
  v_local = vec2(instance1.z < 0.0 ? 1.0 - corner.x : corner.x, corner.y);
  v_codes = instance1.xy;
  v_params = instance2;

  float r = radians(instance0.w);
  vec2 c = (corner - 0.5) * instance0.z;
  // Clockwise on screen, y down: the same turn texture_draw_ex gives.
  vec2 world = instance0.xy + vec2(c.x * cos(r) - c.y * sin(r), c.x * sin(r) + c.y * cos(r));
  gl_Position = mvp * vec4(world, 0.0, 1.0);
}
