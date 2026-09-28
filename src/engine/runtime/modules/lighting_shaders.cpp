#include "lighting_internal.h"

namespace njin::light_impl {
const char *const light_vs = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
out vec2 world;
void main() {
  world = vertexPosition.xy;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// One light, all kinds, added to the linear HDR image. `world` is the position
// of the pixel in the world; the G-buffers and the image share a size and a
// camera, so a fragment reads what is under it by its own window coordinate.
//
// The surface faces the viewer, who looks straight down the z axis (an
// orthographic camera): V = (0, 0, 1). N comes from the normal map, the light
// direction from the light's height above the plane. The BRDF is Cook-Torrance
// with a GGX distribution, Smith-Schlick geometry and Fresnel-Schlick, and the
// diffuse part is scaled by (1 - F)(1 - metallic) so the two never reflect more
// than arrives.
const char *const light_fs = R"(#version 330
#define BUCKETS 32
#define PI 3.14159265359
in vec2 world;
uniform sampler2D albedo;    // the world image, sRGB
uniform sampler2D normals;   // rgb: a normal map, green up
uniform sampler2D material;  // r: metallic, g: roughness, b: ambient occlusion (raylib's "MRA")
uniform vec2 target_size;
uniform int use_normals;
uniform int use_material;
uniform int kind;          // 0 point, 1 spot, 2 directional
uniform int falloff;       // 0 physical, 1 linear, 2 smooth, 3 none
uniform vec2 lpos;
uniform vec2 ldir;         // unit: where a spot points, where directional light travels
uniform vec3 lcolor;       // colour * intensity, linear
uniform float radius;
uniform float source_size;
uniform float height;
uniform float elevation;   // directional, radians
uniform float cone_outer;  // cosine of the outer half angle
uniform float cone_inner;  // cosine of the inner half angle (larger)
uniform float reach;
// The occluder edges that can shadow this light, sorted into BUCKETS buckets: for a
// point or spot light the angular sectors around it, for a directional light the
// strips across the rays. The bucket of a pixel holds the edges its ray to the
// light can meet, one per texel: xy start, zw end, in the world.
uniform sampler2D edges;
uniform sampler2D pixel_shadows; // pixel-perfect shadows: a row per light, a column per angle (or strip), rg = the
                                 // first solid run's entry and exit, as a share of `1 / pixel_inv`
uniform int pixel_row;           // this light's row, or -1
uniform float pixel_columns;
uniform float pixel_inv;         // normalises a distance: a point light 1 / radius, the sun 1 / the along range
uniform float pixel_off;         // and where it starts (the sun's along coordinate at the start of the map)
uniform float pixel_tol;         // a texel and a half, in the same units
uniform int bucket_base;   // the first row of this light in `edges`
uniform int bucket_count[BUCKETS];
uniform int has_shadows;
uniform vec2 bin_axis;     // directional: the axis across the rays
uniform float bin_lo;      // and where the first strip starts along it
uniform float bin_inv;     // strips per world unit
out vec4 finalColor;

int edge_row = 0;   // the row and the number of edges of this pixel's bucket
int edge_count = 0;

float cross2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

// True when the segment p -> s enters an occluder through one of its edges. An
// edge runs with the solid on its left (its outward normal is (y, -x)), so a
// ray that leaves a solid, because p is inside it, is not stopped by it.
bool blocked(vec2 p, vec2 s) {
  vec2 r = s - p;
  for (int i = 0; i < edge_count; i++) {
    vec4 seg = texelFetch(edges, ivec2(i, edge_row), 0);
    vec2 c = seg.xy;
    vec2 e = seg.zw - c;
    float den = cross2(r, e);
    if (abs(den) < 1e-5)
      continue;
    if (dot(r, vec2(e.y, -e.x)) >= 0.0)
      continue;
    vec2 q = c - p;
    float t = cross2(q, e) / den;
    float u = cross2(q, r) / den;
    if (t > 0.001 && t < 0.999 && u >= 0.0 && u <= 1.0)
      return true;
  }
  return false;
}

float hash(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

// Share of the light that reaches p: rays to points spread over the light's
// size, so the shadow is sharp next to what casts it and soft far away.
float visibility(vec2 p, vec2 target, vec2 across, float half_width) {
  if (edge_count == 0)
    return 1.0;
  if (half_width <= 0.5)
    return blocked(p, target) ? 0.0 : 1.0;
  // Three probes (the two ends of the light and its middle) settle most pixels:
  // all clear is lit, all blocked is shadow. Only a pixel in a penumbra, where
  // they disagree, takes the full sampling.
  bool centre = blocked(p, target);
  bool left = blocked(p, target - across * half_width);
  bool right = blocked(p, target + across * half_width);
  if (centre == left && centre == right)
    return centre ? 0.0 : 1.0;
  float jitter = hash(gl_FragCoord.xy);
  float lit = 0.0;
  for (int i = 0; i < 8; i++) {
    float f = (float(i) + jitter) / 8.0 * 2.0 - 1.0;
    lit += blocked(p, target + across * (f * half_width)) ? 0.0 : 1.0;
  }
  return lit / 8.0;
}

// Pixel-perfect shadow (mattdesl, "2D Pixel-Perfect Shadows"): the light's 1D shadow map holds, for each angle,
// where the first occluder starts and ends. A receiver beyond the end is in shadow; one before it, or inside it
// (the object itself), is lit. The soft edge is percentage-closer filtering across neighbouring angles, as wide
// as the penumbra the light's size makes from the average blocker depth (percentage-closer soft shadows).
vec2 shadow_at(float t) {
  float col = floor(clamp(t, 0.0, 0.99999) * pixel_columns);
  return texelFetch(pixel_shadows, ivec2(int(col), pixel_row), 0).rg;
}

// Half the penumbra, as a share of the coordinate (angle / 2 pi, or the across range), for a receiver at
// normalised depth `nd` behind a blocker at `db`. `world_r` is what a normalised 1 is, in world units.
float penumbra(float nd, float db, float world_r) {
  db = max(db, 0.002);
  float width = source_size * (nd - db) / db;               // across the receiver, world units (point light)
  if (kind == 2)
    width = source_size * (nd - db) * world_r / reach;      // the sun's rays are nearly parallel
  float half_span = kind == 2 ? bin_inv / float(BUCKETS) : 1.0 / (2.0 * PI * max(nd * world_r, 1.0));
  return width * half_span;
}

float pixel_visibility(float t, float nd) {
  float world_r = 1.0 / pixel_inv;
  bool wrap = kind != 2;
  // Inside the first solid on its ray: the object itself, lit whatever its neighbours' rays do.
  for (int i = -1; i <= 1; i++) {   // the neighbouring columns too: a ray at a slant can skirt a corner
    vec2 own = shadow_at(t + float(i) / pixel_columns);
    if (nd >= own.x - pixel_tol && nd <= own.y + pixel_tol)
      return 1.0;
  }
  // Search for blockers across the widest penumbra the light could make here.
  float search = kind == 2 ? source_size * nd * world_r / reach * bin_inv / float(BUCKETS)
                           : source_size / max(nd * world_r, 1.0) / (2.0 * PI);
  search = clamp(search, 0.5 / pixel_columns, 0.25);
  float blocker = 0.0;
  float found = 0.0;
  for (int i = -2; i <= 2; i++) {
    float u = t + search * float(i) / 2.0;
    u = wrap ? fract(u) : u;
    vec2 be = shadow_at(u);
    if (nd > be.y + pixel_tol) {
      blocker += be.x;
      found += 1.0;
    }
  }
  if (found < 0.5)
    return 1.0;
  float w = clamp(penumbra(nd, blocker / found, world_r), 0.6 / pixel_columns, 0.25);
  float lit = 0.0;
  float weight = 0.0;
  for (int i = -4; i <= 4; i++) {
    float u = t + w * float(i) / 4.0;
    u = wrap ? fract(u) : u;
    float k = 5.0 - abs(float(i));
    lit += k * (nd <= shadow_at(u).y + pixel_tol ? 1.0 : 0.0);
    weight += k;
  }
  return lit / weight;
}

float distribution_ggx(float nh, float a) {
  float a2 = a * a;
  float d = nh * nh * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}

float geometry_schlick(float x, float k) { return x / (x * (1.0 - k) + k); }

void main() {
  float att = 1.0;
  float cone = 1.0;
  float vis = 1.0;
  vec3 l3;
  float pixel_t = 0.0;  // where in the light's shadow row this pixel lies, and how deep
  float pixel_nd = 0.0;
  if (kind == 2) {
    vec2 back = -ldir;
    pixel_t = (dot(world, bin_axis) - bin_lo) * bin_inv / float(BUCKETS);
    pixel_nd = (dot(world, ldir) - pixel_off) * pixel_inv;
    l3 = normalize(vec3(back * cos(elevation), sin(elevation)));
    if (has_shadows != 0) {
      int bucket = clamp(int(floor((dot(world, bin_axis) - bin_lo) * bin_inv)), 0, BUCKETS - 1);
      edge_row = bucket_base + bucket;
      edge_count = bucket_count[bucket];
    }
    vis = visibility(world, world + back * reach, vec2(-ldir.y, ldir.x), source_size);
  } else {
    vec2 to_light = lpos - world;
    float d = length(to_light);
    if (d >= radius)
      discard;
    float x = d / radius;
    float window = 1.0 - x * x * x * x;
    window *= window;
    if (falloff == 0) {
      float s = max(source_size, 1.0);
      att = window / (1.0 + (d / s) * (d / s));
    } else if (falloff == 1) {
      att = 1.0 - x;
    } else if (falloff == 2) {
      float a = 1.0 - x * x;
      att = a * a;
    } else {
      att = 1.0 - smoothstep(0.9, 1.0, x);
    }
    vec2 dir = d > 0.0001 ? to_light / d : vec2(0.0, 1.0);
    if (kind == 1)
      cone = smoothstep(cone_outer, cone_inner, dot(-dir, ldir));
    // Too little reaches this pixel to see: skip the shadow rays, the costly part.
    float peak = max(lcolor.r, max(lcolor.g, lcolor.b));
    if (att * cone * peak < 0.006)
      discard;
    l3 = normalize(vec3(to_light, height));
    pixel_t = (atan(-to_light.y, -to_light.x) + PI) / (2.0 * PI);
    pixel_nd = d * pixel_inv;
    if (has_shadows != 0) {
      float angle = atan(-to_light.y, -to_light.x); // where the pixel lies, seen from the light
      int bucket = clamp(int(floor((angle + PI) * (float(BUCKETS) / (2.0 * PI)))), 0, BUCKETS - 1);
      edge_row = bucket_base + bucket;
      edge_count = bucket_count[bucket];
    }
    vis = visibility(world, lpos, vec2(-dir.y, dir.x), source_size);
  }
  if (pixel_row >= 0 && vis > 0.0)
    vis *= pixel_visibility(pixel_t, pixel_nd);
  if (vis <= 0.0)
    discard;

  vec2 uv = gl_FragCoord.xy / target_size;
  vec3 base = pow(texture(albedo, uv).rgb, vec3(2.2)); // to linear
  vec3 n = vec3(0.0, 0.0, 1.0);
  if (use_normals != 0) {
    n = texture(normals, uv).xyz * 2.0 - 1.0;
    n.y = -n.y; // normal maps have green up, the world has y down
    n = normalize(n);
  }
  float rough = 0.8, metal = 0.0;
  if (use_material != 0) {
    vec3 m = texture(material, uv).rgb;
    metal = m.r;
    rough = m.g;
  }
  rough = clamp(rough, 0.05, 1.0);

  vec3 v = vec3(0.0, 0.0, 1.0);
  vec3 h = normalize(l3 + v);
  float nl = max(dot(n, l3), 0.0);
  float nv = max(dot(n, v), 0.001);
  float nh = max(dot(n, h), 0.0);
  float vh = max(dot(v, h), 0.0);
  float a = rough * rough;
  float k = (rough + 1.0) * (rough + 1.0) / 8.0;
  vec3 f0 = mix(vec3(0.04), base, metal);
  vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
  float dgg = distribution_ggx(nh, a) * geometry_schlick(nl, k) * geometry_schlick(nv, k);
  vec3 spec = fresnel * dgg / (4.0 * nl * nv + 0.001);
  vec3 kd = (1.0 - fresnel) * (1.0 - metal);
  vec3 radiance = lcolor * (att * cone * vis);
  finalColor = vec4((kd * base + spec * PI) * radiance * nl, 1.0);
}
)";

// The start of the light image: ambient light on the albedo, less where the
// surface is occluded. A metal has no diffuse colour, but it reflects the
// ambient light, tinted by itself (less so the rougher it is). What glows
// (the emissive image, up to 8 times the picture's own colour) is added. Drawn
// with the world image as texture0.
const char *const ambient_fs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform sampler2D material;
uniform sampler2D emissive_map;
uniform vec2 target_size;
uniform int use_material;
uniform int use_emissive;
uniform vec3 ambient; // linear
out vec4 finalColor;
void main() {
  vec2 uv = gl_FragCoord.xy / target_size;
  vec3 base = pow(texture(texture0, uv).rgb, vec3(2.2));
  vec3 m = use_material != 0 ? texture(material, uv).rgb : vec3(0.0, 0.8, 1.0); // metallic, roughness, occlusion
  vec3 f0 = mix(vec3(0.04), base, m.r);
  vec3 reflected = f0 * (1.0 - 0.5 * m.g);
  vec3 glow = use_emissive != 0 ? pow(texture(emissive_map, uv).rgb, vec3(2.2)) * 8.0 : vec3(0.0);
  finalColor = vec4(ambient * (base * (1.0 - m.r) + reflected) * m.b + glow, 1.0);
}
)";

// Linear HDR to the screen: the exposure, then a tonemap (0 a shoulder: the
// tones below 0.6 stay as they are and the ones above roll off smoothly towards
// 1; 1 Reinhard; 2 the ACES filmic curve, Narkowicz's fit), then gamma. The
// alpha is the world's.
const char *const final_fs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform sampler2D hdr;
uniform vec2 target_size;
uniform float exposure;
uniform int tonemap;
out vec4 finalColor;
void main() {
  vec2 uv = gl_FragCoord.xy / target_size;
  vec3 c = texture(hdr, uv).rgb * exposure;
  if (tonemap == 1) {
    c = c / (1.0 + c);
  } else if (tonemap == 2) {
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
  } else {
    vec3 over = max(c - 0.6, vec3(0.0));
    c = min(c, vec3(0.6)) + 0.4 * (1.0 - exp(-over / 0.4));
  }
  finalColor = vec4(pow(c, vec3(1.0 / 2.2)), texture(texture0, uv).a);
}
)";
// The 1D shadow map of one light (mattdesl, "2D Pixel-Perfect Shadows"): one fragment per column, marching a
// ray through the occluder map, one texel at a time, from the light (or from the start of a strip, for the sun).
// The result is where the first solid run starts and ends, as a share of `max_len`; 1 and 1 when there is none.
// A light that sits inside a solid first leaves it, so it is not blocked by its own object.
const char *const march_fs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D occluders;
uniform vec2 map_size;   // texels
uniform int mode;        // 0 around a point, 1 along parallel strips
uniform float columns;
uniform float alpha;     // what counts as solid
uniform vec2 origin;     // point: the light, in texels (y down)
uniform float max_len;   // texels to march
uniform vec2 rot;        // cos, sin of the camera's rotation: world direction to texel direction
uniform vec2 strip0;     // strips: where strip 0 starts, and the step to the next one, in texels
uniform vec2 strip_step;
uniform vec2 dir;        // strips: the unit direction of the rays, in texels
out vec4 finalColor;

bool solid(vec2 p) {
  if (p.x < 0.0 || p.y < 0.0 || p.x >= map_size.x || p.y >= map_size.y)
    return false;
  return texelFetch(occluders, ivec2(int(p.x), int(map_size.y) - 1 - int(p.y)), 0).a >= alpha;
}

void main() {
  float c = floor(gl_FragCoord.x);
  vec2 start;
  vec2 step_dir;
  if (mode == 0) {
    float a = (c + 0.5) / columns * 6.28318530718 - 3.14159265359;
    vec2 w = vec2(cos(a), sin(a));
    step_dir = vec2(rot.x * w.x - rot.y * w.y, rot.y * w.x + rot.x * w.y);
    start = origin;
  } else {
    start = strip0 + strip_step * (c + 0.5);
    step_dir = dir;
  }
  // Half a texel at a time: at a slant a whole-texel step can jump over the corner of a pixel.
  const float STEP = 0.5;
  int n = int(min(max_len / STEP, 4096.0));
  int i = 0;
  if (mode == 0) {
    for (int k = 0; k < 4096; k++) {           // leave the light's own solid first
      if (i >= n || !solid(start + step_dir * ((float(i) + 0.5) * STEP)))
        break;
      i++;
    }
  }
  for (int k = 0; k < 4096; k++) {             // the first solid texel
    if (i >= n || solid(start + step_dir * ((float(i) + 0.5) * STEP)))
      break;
    i++;
  }
  float entry = 1.0;
  float leave = 1.0;
  if (i < n) {
    entry = float(i) * STEP / max_len;
    int j = i;
    for (int k = 0; k < 4096; k++) {           // and where that run ends
      if (j >= n || !solid(start + step_dir * ((float(j) + 0.5) * STEP)))
        break;
      j++;
    }
    leave = float(j) * STEP / max_len;
  }
  finalColor = vec4(entry, leave, 0.0, 1.0);
}
)";
} // namespace njin::light_impl
