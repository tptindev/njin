#version 330

// One instance per person. The vertex shader decodes the DNA (layout in
// dna.cpp) into the person's colours, finds their body cell and head cell, and
// lifts (jump) or lays down (lie) the quad. The CPU only sends position, cell,
// flags and the two DNA words.
in vec3 vertexPosition;
in vec4 instance0; // feet x, y (world); body cell + 1, negative when mirrored; head direction + 8 if selected
in vec4 instance1; // DNA word 0, DNA word 1, lift (world units off the ground), rotation (radians)
uniform mat4 mvp;

uniform vec2 u_cell;             // body cell size, sheet pixels
uniform vec2 u_sheet;            // body sheet size, pixels
uniform float u_cols;            // body cells per sheet row
uniform float u_rows_per_build;  // sheet rows per build
uniform vec2 u_head_cell;        // head cell size, pixels
uniform float u_foot;            // feet line, pixels above the bottom of a body cell
uniform float u_world;           // world units per sheet pixel
uniform float u_scale;           // sheet pixels per shape unit
uniform float u_bottom;          // shape-space y of the bottom of a body cell
uniform sampler2D sheet_b;       // body sheet B: its cell corners hold the head centre

out vec2 v_q;                    // body cell pixels from the top-left (unmirrored); y may pass the cell when lifted
flat out vec2 v_origin;          // body cell top-left in the sheet
flat out vec2 v_head;            // head centre in the body cell
flat out vec2 v_head_origin;     // head cell top-left in the head sheet
flat out float v_lift;           // lift, cell pixels
flat out float v_lie;
flat out float v_selected;
flat out vec3 v_cloth;
flat out vec3 v_skin;
flat out vec3 v_hair;
flat out vec3 v_far;
flat out vec3 v_near;
flat out vec3 v_shoe;

const vec3 SKIN[16] = vec3[16](
  vec3(1.00, 0.87, 0.77), vec3(0.98, 0.82, 0.71), vec3(0.96, 0.78, 0.68), vec3(0.93, 0.75, 0.62),
  vec3(0.90, 0.72, 0.58), vec3(0.87, 0.68, 0.53), vec3(0.82, 0.62, 0.47), vec3(0.78, 0.57, 0.42),
  vec3(0.72, 0.52, 0.38), vec3(0.66, 0.47, 0.33), vec3(0.60, 0.42, 0.30), vec3(0.54, 0.37, 0.26),
  vec3(0.48, 0.33, 0.23), vec3(0.42, 0.29, 0.21), vec3(0.36, 0.25, 0.18), vec3(0.30, 0.21, 0.16));

const vec3 HAIR[16] = vec3[16](
  vec3(0.18, 0.15, 0.16), vec3(0.10, 0.09, 0.09), vec3(0.24, 0.17, 0.13), vec3(0.33, 0.22, 0.15),
  vec3(0.45, 0.30, 0.19), vec3(0.58, 0.42, 0.26), vec3(0.76, 0.62, 0.40), vec3(0.88, 0.78, 0.55),
  vec3(0.62, 0.26, 0.14), vec3(0.74, 0.36, 0.18), vec3(0.70, 0.70, 0.72), vec3(0.90, 0.90, 0.90),
  vec3(0.20, 0.22, 0.40), vec3(0.62, 0.30, 0.52), vec3(0.28, 0.55, 0.50), vec3(0.85, 0.48, 0.62));

// Sleeves and trousers: the near side as given, the far side darker.
const vec3 LIMB[16] = vec3[16](
  vec3(0.27, 0.23, 0.25), vec3(0.16, 0.16, 0.18), vec3(0.20, 0.26, 0.40), vec3(0.28, 0.36, 0.52),
  vec3(0.35, 0.27, 0.20), vec3(0.45, 0.38, 0.28), vec3(0.30, 0.34, 0.24), vec3(0.40, 0.42, 0.44),
  vec3(0.42, 0.18, 0.20), vec3(0.22, 0.30, 0.30), vec3(0.55, 0.50, 0.42), vec3(0.12, 0.12, 0.14),
  vec3(0.34, 0.24, 0.36), vec3(0.50, 0.30, 0.22), vec3(0.24, 0.20, 0.17), vec3(0.62, 0.60, 0.58));

const vec3 SHOE[8] = vec3[8](
  vec3(0.12, 0.10, 0.11), vec3(0.30, 0.20, 0.13), vec3(0.85, 0.85, 0.83), vec3(0.45, 0.12, 0.10),
  vec3(0.15, 0.18, 0.30), vec3(0.50, 0.40, 0.28), vec3(0.22, 0.22, 0.22), vec3(0.62, 0.52, 0.20));

vec3 hsv2rgb(vec3 c) {
  vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
  return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

void main() {
  uint w0 = uint(instance1.x + 0.5);
  uint w1 = uint(instance1.y + 0.5);
  uint build     = w0 & 3u;
  uint hairStyle = (w0 >> 2u) & 7u;
  uint height    = (w0 >> 5u) & 15u;
  uint skin      = (w0 >> 9u) & 15u;
  uint hairColor = (w0 >> 13u) & 15u;
  uint limb      = (w0 >> 17u) & 15u;
  uint shoe      = (w0 >> 21u) & 7u;
  uint hue       = w1 & 63u;
  uint sat       = (w1 >> 6u) & 3u;
  uint val       = (w1 >> 8u) & 3u;

  v_skin  = SKIN[skin];
  v_hair  = HAIR[hairColor];
  v_near  = LIMB[limb];
  v_far   = LIMB[limb] * 0.55;
  v_shoe  = SHOE[shoe];
  v_cloth = hsv2rgb(vec3(float(hue) / 64.0, 0.24 + 0.12 * float(sat), 0.40 + 0.14 * float(val)));

  // Body cell: the row block from the build, the rest from the cell number.
  float cell = abs(instance0.z) - 1.0;
  bool mirrored = instance0.z < 0.0;
  float row = float(build) * u_rows_per_build + floor(cell / u_cols);
  v_origin = vec2(mod(cell, u_cols), row) * u_cell;

  // Head: its centre was baked into the cell's corner texel (render texture: rows from the bottom).
  ivec2 corner = ivec2(int(v_origin.x + 0.5), int(u_sheet.y + 0.5) - 1 - int(v_origin.y + 0.5));
  v_head = texelFetch(sheet_b, corner, 0).gb * u_cell;
  float headDir = mod(instance0.w, 8.0);
  v_selected = step(8.0, instance0.w);
  v_head_origin = vec2(headDir, float(hairStyle)) * u_head_cell;

  // Taller or shorter by the height gene; the feet stay on the ground.
  float stretch = 0.92 + 0.16 * float(height) / 15.0;
  float lift = instance1.z / (u_world * stretch);
  v_lift = lift;
  v_lie = instance1.w != 0.0 ? 1.0 : 0.0;

  // The quad grows downwards by the lift, so the body rises and the shadow stays.
  vec2 c = vertexPosition.xy; // 0..1, y down
  float lx = mirrored ? 1.0 - c.x : c.x;
  float h = u_cell.y + lift;
  v_q = vec2(lx * u_cell.x, c.y * h);
  vec2 rel = vec2((c.x - 0.5) * u_cell.x * u_world, (c.y * h - (u_cell.y - u_foot) - lift) * u_world * stretch);

  // Lying down: turn the figure about its hips.
  if (instance1.w != 0.0) {
    float hipQ = u_cell.y - (-0.020 - u_bottom) * u_scale;
    vec2 pivot = vec2(0.0, (hipQ - (u_cell.y - u_foot)) * u_world * stretch);
    float cs = cos(instance1.w), sn = sin(instance1.w);
    vec2 d = rel - pivot;
    rel = vec2(cs * d.x - sn * d.y, sn * d.x + cs * d.y);
  }
  gl_Position = mvp * vec4(instance0.xy + rel, 0.0, 1.0);
}
