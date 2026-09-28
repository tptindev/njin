#version 330

// Colours the baked body and head cells with the person's own palette, over a
// soft ground shadow. Layers, back to front: shadow, hair behind the body,
// body, head.
in vec2 v_q;
flat in vec2 v_origin;
flat in vec2 v_head;
flat in vec2 v_head_origin;
flat in float v_lift;
flat in float v_lie;
flat in float v_selected;
flat in vec3 v_cloth;
flat in vec3 v_skin;
flat in vec3 v_hair;
flat in vec3 v_far;
flat in vec3 v_near;
flat in vec3 v_shoe;
out vec4 finalColor;

uniform sampler2D texture0;   // body A: cloth, limb far, limb near
uniform sampler2D sheet_b;    // body B: shoe
uniform sampler2D head_sheet; // skin, hair in front, hair behind
uniform vec2 u_cell;
uniform vec2 u_sheet;
uniform vec2 u_head_cell;
uniform vec2 u_head_center;
uniform vec2 u_head_sheet;
uniform float u_scale;
uniform float u_bottom;

// A render texture sample, `q` pixels into the cell at `origin`, kept inside the cell.
vec4 cellSample(sampler2D t, vec2 origin, vec2 q, vec2 cell, vec2 size) {
  vec2 uv = (origin + clamp(q, vec2(0.5), cell - 0.5)) / size;
  return textureLod(t, vec2(uv.x, 1.0 - uv.y), 0.0);
}

float sdCapsule(vec2 p, vec2 a, vec2 b, float r) {
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h) - r;
}

void main() {
  vec3 body = vec3(0.0);
  float shoe = 0.0;
  if (v_q.y <= u_cell.y) {
    body = cellSample(texture0, v_origin, v_q, u_cell, u_sheet).rgb;
    shoe = cellSample(sheet_b, v_origin, v_q, u_cell, u_sheet).r;
  }
  vec3 head = vec3(0.0);
  vec2 hq = v_q - v_head + u_head_center;
  if (all(greaterThanEqual(hq, vec2(0.0))) && all(lessThanEqual(hq, u_head_cell)))
    head = cellSample(head_sheet, v_head_origin, hq, u_head_cell, u_head_sheet).rgb;

  // Ground shadow, as in the reference sample: smaller when jumping, along the
  // body when lying, a gold ring under the selected person.
  vec2 sp = vec2(v_q.x - 0.5 * u_cell.x, u_cell.y - (v_q.y - v_lift)) / u_scale + vec2(0.0, u_bottom);
  float aa = max(fwidth(sp.x), 1e-5);
  float sel = v_selected;
  float k = 1.0 / (1.0 + v_lift * 0.03);
  float d = v_lie > 0.5 ? sdCapsule(sp, vec2(0.0, -0.072), vec2(0.0, 0.040), 0.012)
                        : sdCapsule(sp, vec2(-mix(0.014, 0.018, sel) * k, -0.080), vec2(mix(0.014, 0.018, sel) * k, -0.080),
                                    mix(0.0035, 0.0055, sel) * k);
  float shadowA = smoothstep(aa, -aa, d) * mix(v_lie > 0.5 ? 0.22 : 0.3, 0.95, sel) * k;
  vec3 shadowC = mix(vec3(0.0), vec3(1.0, 0.78, 0.15), sel);

  // Composite premultiplied layers, back to front.
  vec3 color = shadowC * shadowA;
  float alpha = shadowA;
  color = v_hair * head.b + color * (1.0 - head.b);
  alpha = head.b + alpha * (1.0 - head.b);
  float bodyA = min(1.0, body.r + body.g + body.b + shoe);
  color = body.r * v_cloth + body.g * v_far + body.b * v_near + shoe * v_shoe + color * (1.0 - bodyA);
  alpha = bodyA + alpha * (1.0 - bodyA);
  float headA = min(1.0, head.r + head.g);
  color = head.r * v_skin + head.g * v_hair + color * (1.0 - headA);
  alpha = headA + alpha * (1.0 - headA);

  if (alpha < 0.004) discard;
  finalColor = vec4(color / alpha, alpha);
}
