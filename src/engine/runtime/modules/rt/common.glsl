// What the ray tracing shaders share (render3d_rt_vk.cpp's params_t, the
// same order): the pass, the sun and lamps, the environment, the irradiance
// grid. Included by every rt/*.comp.
layout(set = 0, binding = 256) uniform Params {
  mat4 invViewProj;
  vec4 eye;        // xyz
  vec4 sun;        // direction towards the sun, tan of its angular radius
  vec4 size;       // width, height, shadow rays, frame
  vec4 lamps[3];   // ray traced lamps: position, size (radius of the light's sphere)
  vec4 lampInfo;   // shadow lamp count, denoise radius, 1 when the sun's shadow is traced
  vec4 sunColor;   // linear colour of the sun, w: 1 when it lights
  vec4 ambient;    // linear ambient light (the sky without an environment), w: 1 when envSH holds the environment
  vec4 envSH[9];   // the environment's irradiance (times its intensity); envSH[0].w its rotation, radians
  vec4 lightPos[8];   // lamps lighting what rays hit: position, reach
  vec4 lightColor[8]; // linear colour times intensity, cos of the inner cone
  vec4 lightSpot[8];  // direction, cos of the outer cone (-2 for a point light)
  vec4 lightInfo;     // lamp count, 1 when the grid lights what rays hit
  vec4 gridMin;       // the irradiance grid's corner, the share kept from last frame
  vec4 gridSize;      // its size, intensity
  vec4 gridCount;     // points along x, y, z, rays per point
} p;

const float PI = 3.14159265;

vec3 world_at(ivec2 px, float d) {
  vec2 ndc = (vec2(px) + 0.5) / p.size.xy * 2.0 - 1.0;
  vec4 w = p.invViewProj * vec4(ndc, d * 2.0 - 1.0, 1.0);
  return w.xyz / w.w;
}

// A hash, 0..1, per pixel, ray and frame.
float rnd(uvec3 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z;
  v.y += v.z * v.x;
  v.z += v.x * v.y;
  v ^= v >> 16u;
  v.x += v.y * v.z;
  return float(v.x) * (1.0 / 4294967296.0);
}

#ifdef RT_DEPTH
vec3 world_at(ivec2 px) { return world_at(px, imageLoad(depthImg, px).r); }

// Of the steps to the two neighbours on an axis, the shorter (so an edge does
// not bend the normal); one of zero length (the neighbour clamped to the
// image's edge) does not count.
vec3 shorter(vec3 a, vec3 b) {
  float la = dot(a, a), lb = dot(b, b);
  if (la <= 0.0)
    return b;
  if (lb <= 0.0)
    return a;
  return la < lb ? a : b;
}

// The surface normal of pixel `px` (at `c`) from the depth, facing the eye.
vec3 normal_at(ivec2 px, vec3 c) {
  ivec2 hi = ivec2(p.size.xy) - 1;
  vec3 l = world_at(clamp(px - ivec2(1, 0), ivec2(0), hi)), r = world_at(clamp(px + ivec2(1, 0), ivec2(0), hi));
  vec3 b = world_at(clamp(px - ivec2(0, 1), ivec2(0), hi)), t = world_at(clamp(px + ivec2(0, 1), ivec2(0), hi));
  vec3 n = cross(shorter(r - c, c - l), shorter(t - c, c - b));
  vec3 to_eye = normalize(p.eye.xyz - c);
  if (dot(n, n) < 1e-20)
    return to_eye;
  n = normalize(n);
  return dot(n, to_eye) < 0.0 ? -n : n;
}
#endif

#ifdef RT_SCENE
// Whether anything lies on the ray within `tmax`.
bool blocked(vec3 o, vec3 d, float tmax) {
  rayQueryEXT q;
  rayQueryInitializeEXT(q, scene, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFF, o, 0.0, d, tmax);
  while (rayQueryProceedEXT(q)) {
  }
  return rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}
#endif

#ifdef RT_SHADE
// Each instance of the scene (render3d_rt_vk.cpp's inst_t): its mesh's
// vertex and index buffers by address, index size, colour and glow.
struct inst_t {
  uvec2 vb;
  uvec2 ib;
  uvec4 kind;    // x: 0 no indices, 1 16-bit, 2 32-bit; y: 1 drawn from both sides
  vec4 albedo;   // linear
  vec4 emission; // linear, strength applied
};
layout(set = 0, binding = 1, std430) readonly buffer Insts { inst_t insts[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Floats { float v[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Words { uint w[]; };

uint index_of(inst_t it, uint k) {
  if (it.kind.x == 0u)
    return k;
  Words ib = Words(it.ib);
  if (it.kind.x == 2u)
    return ib.w[k];
  uint word = ib.w[k >> 1];
  return (k & 1u) == 0u ? word & 0xFFFFu : word >> 16;
}

vec3 vertex_of(inst_t it, uint i) {
  Floats vb = Floats(it.vb);
  return vec3(vb.v[i * 3u], vb.v[i * 3u + 1u], vb.v[i * 3u + 2u]);
}

// What a ray met: where, its face's normal (towards the ray), its instance.
struct hit_t {
  bool hit;
  bool front; // met the face's front (counter-clockwise, as OpenGL draws it): from outside
  bool two_sided; // the surface is drawn from both sides: it has no inside
  vec3 pos;
  vec3 n;
  int inst;
};

hit_t trace_closest(vec3 o, vec3 d, float tmax) {
  hit_t h;
  h.hit = false;
  rayQueryEXT q;
  rayQueryInitializeEXT(q, scene, gl_RayFlagsOpaqueEXT, 0xFF, o, 0.0, d, tmax);
  while (rayQueryProceedEXT(q)) {
  }
  if (rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT)
    return h;
  h.hit = true;
  h.front = rayQueryGetIntersectionFrontFaceEXT(q, true);
  float t = rayQueryGetIntersectionTEXT(q, true);
  h.pos = o + d * t;
  h.inst = rayQueryGetIntersectionInstanceCustomIndexEXT(q, true);
  uint prim = uint(rayQueryGetIntersectionPrimitiveIndexEXT(q, true));
  inst_t it = insts[h.inst];
  h.two_sided = it.kind.y != 0u;
  vec3 a = vertex_of(it, index_of(it, prim * 3u));
  vec3 b = vertex_of(it, index_of(it, prim * 3u + 1u));
  vec3 c = vertex_of(it, index_of(it, prim * 3u + 2u));
  mat4x3 o2w = rayQueryGetIntersectionObjectToWorldEXT(q, true);
  mat3 m = mat3(o2w[0], o2w[1], o2w[2]);
  vec3 n = normalize(transpose(inverse(m)) * cross(b - a, c - a));
  // Faces are seen from both sides (the scene does not promise a winding).
  h.n = dot(n, d) > 0.0 ? -n : n;
  return h;
}

// Light from all around along `d` where nothing is in the way: the
// environment's irradiance over pi, or the ambient colour.
vec3 sky(vec3 d) {
  if (p.ambient.w < 0.5)
    return p.ambient.rgb;
  float c = cos(p.envSH[0].w), s = sin(p.envSH[0].w);
  vec3 n = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
  vec3 e = p.envSH[0].xyz * 0.886227 + p.envSH[1].xyz * (1.023328 * n.y) + p.envSH[2].xyz * (1.023328 * n.z) +
           p.envSH[3].xyz * (1.023328 * n.x) + p.envSH[4].xyz * (0.858086 * n.x * n.y) +
           p.envSH[5].xyz * (0.858086 * n.y * n.z) + p.envSH[6].xyz * (0.247708 * (3.0 * n.z * n.z - 1.0)) +
           p.envSH[7].xyz * (0.858086 * n.x * n.z) + p.envSH[8].xyz * (0.429043 * (n.x * n.x - n.y * n.y));
  return max(e, vec3(0.0)) / PI;
}

#define GRID_COUNT p.gridCount
#define GRID_SIZE p.gridSize
#define GRID_MIN p.gridMin
#define GRID_FETCH_DEPTH(at) imageLoad(gridDepth, at)

// The grid's spacing between points along each axis.
vec3 grid_spacing() {
  vec3 cnt = GRID_COUNT.xyz;
  return GRID_SIZE.xyz / max(cnt - 1.0, vec3(1.0));
}

// Where grid point `q` (its indices) stands.
vec3 grid_point(vec3 q) {
  vec3 cnt = GRID_COUNT.xyz;
  vec3 t = mix(vec3(0.5), q / max(cnt - 1.0, vec3(1.0)), vec3(greaterThan(cnt, vec3(1.0))));
  return GRID_MIN.xyz + GRID_SIZE.xyz * t;
}

// Octahedral coordinates (0..1) of direction `d`.
vec2 grid_oct(vec3 d) {
  d /= abs(d.x) + abs(d.y) + abs(d.z);
  vec2 e = d.z >= 0.0 ? d.xy : (1.0 - abs(d.yx)) * vec2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
  return e * 0.5 + 0.5;
}

// Point `col`'s mean distance to what is round it towards `d`, and the mean
// square: its 16 x 16 octahedral depth tile, filtered by hand.
vec2 grid_depth(int col, vec3 d) {
  vec2 t = clamp(grid_oct(d) * 16.0 - 0.5, vec2(0.0), vec2(15.0));
  ivec2 a = ivec2(floor(t));
  ivec2 b = min(a + 1, ivec2(15));
  vec2 f = t - vec2(a);
  vec2 m00 = GRID_FETCH_DEPTH(ivec2(col * 16 + a.x, a.y)).rg, m10 = GRID_FETCH_DEPTH(ivec2(col * 16 + b.x, a.y)).rg;
  vec2 m01 = GRID_FETCH_DEPTH(ivec2(col * 16 + a.x, b.y)).rg, m11 = GRID_FETCH_DEPTH(ivec2(col * 16 + b.x, b.y)).rg;
  return mix(mix(m00, m10, f.x), mix(m01, m11, f.x), f.y);
}

// How much point `col` at `at` sees a surface at `pos` facing `n` (DDGI's
// weights): less as the surface turns away from it, and by Chebyshev's
// bound from its depths when something lies between them.
float grid_visibility(int col, vec3 at, vec3 pos, vec3 n) {
  vec3 s = grid_spacing();
  vec3 biased = pos + n * (0.2 * min(s.x, min(s.y, s.z)));
  vec3 d = biased - at;
  float r = length(d);
  if (r < 1e-4)
    return 1.0;
  d /= r;
  float wrap = (dot(-d, n) + 1.0) * 0.5;
  float w = wrap * wrap + 0.2;
  vec2 m = grid_depth(col, d);
  if (r > m.x) {
    float variance = abs(m.y - m.x * m.x);
    float e = r - m.x;
    float c = variance / (variance + e * e);
    w *= max(c * c * c, 0.0);
  }
  // Tiny weights crushed further, so a hidden point's light does not creep in.
  if (w < 0.2)
    w *= w * w / 0.04;
  return max(w, 1e-6);
}

// The grid's ambient light at `pos` facing `n` (render3d.cpp's grid_irradiance),
// or the ambient colour outside it.
vec3 ambient_at(vec3 pos, vec3 n) {
  vec3 a = p.ambient.rgb;
  if (p.lightInfo.y < 0.5)
    return a;
  vec3 rel = (pos - p.gridMin.xyz) / max(p.gridSize.xyz, vec3(1e-4));
  if (any(lessThan(rel, vec3(0.0))) || any(greaterThan(rel, vec3(1.0))))
    return a;
  vec3 cnt = p.gridCount.xyz;
  vec3 g = rel * max(cnt - 1.0, vec3(0.0));
  vec3 g0 = floor(g);
  vec3 f = g - g0;
  vec3 n2 = n * n;
  int dx = n.x >= 0.0 ? 0 : 1, dy = n.y >= 0.0 ? 2 : 3, dz = n.z >= 0.0 ? 4 : 5;
  vec3 sum = vec3(0.0);
  float wsum = 0.0;
  for (int c = 0; c < 8; c++) {
    vec3 o = vec3(float(c & 1), float((c >> 1) & 1), float((c >> 2) & 1));
    vec3 q = min(g0 + o, max(cnt - 1.0, vec3(0.0)));
    float w = (o.x > 0.5 ? f.x : 1.0 - f.x) * (o.y > 0.5 ? f.y : 1.0 - f.y) * (o.z > 0.5 ? f.z : 1.0 - f.z);
    int col = int(q.x + cnt.x * (q.y + cnt.y * q.z));
    // A point inside something (alpha 0) does not count, one that does not see
    // `pos` hardly.
    w *= imageLoad(gridImg, ivec2(col, 0)).a;
    if (w <= 0.0)
      continue;
    w *= grid_visibility(col, grid_point(q), pos, n);
    sum += w * (n2.x * imageLoad(gridImg, ivec2(col, dx)).rgb + n2.y * imageLoad(gridImg, ivec2(col, dy)).rgb +
                n2.z * imageLoad(gridImg, ivec2(col, dz)).rgb);
    wsum += w;
  }
  // Where nearly every point is hidden the few left are not trusted alone:
  // the sum is divided by at least 0.05, so it darkens instead of flaring.
  return wsum > 1e-4 ? sum / max(wsum, 0.05) * p.gridSize.w : a;
}

// The light leaving a hit towards where the ray came from, in linear light: a
// matte surface of the instance's colour under the sun (through its shadow),
// the lamps (unshadowed) and the ambient light, plus its glow.
vec3 shade_hit(hit_t h) {
  inst_t it = insts[h.inst];
  vec3 n = h.n;
  vec3 o = h.pos + n * 0.01;
  vec3 light = ambient_at(h.pos, n);
  if (p.sunColor.w > 0.5) {
    vec3 l = normalize(p.sun.xyz);
    float nl = dot(n, l);
    if (nl > 0.0 && !blocked(o, l, 10000.0))
      light += p.sunColor.rgb * nl;
  }
  for (int i = 0; i < 8; i++) {
    if (i >= int(p.lightInfo.x))
      break;
    vec3 d = p.lightPos[i].xyz - h.pos;
    float dist = length(d);
    float reach = p.lightPos[i].w;
    if (dist >= reach)
      continue;
    vec3 li = d / max(dist, 1e-5);
    float fall = 1.0 - dist / reach;
    float att = fall * fall * max(dot(n, li), 0.0);
    if (p.lightSpot[i].w > -1.5) {
      float co = p.lightSpot[i].w, ci = p.lightColor[i].w;
      att *= clamp((dot(-li, p.lightSpot[i].xyz) - co) / max(ci - co, 1e-4), 0.0, 1.0);
    }
    light += p.lightColor[i].rgb * att;
  }
  return it.albedo.rgb * light + it.emission.rgb;
}
#endif
