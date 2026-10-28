// The built-in looks of 3D particles (particles3d_desc::style): one vertex
// shader passing each particle's corners on in world space, so noise can be
// sampled where the particle is and stays put while it moves through it, and
// a fragment shader per look. Compiled the first time a look is drawn.
//
// The particle's colour alpha falls with its age, so the shaders read it as
// age. Additive particles are blended as source alpha, one: the colour goes
// out as it is and the alpha carries the strength.
#include "particles3d_looks.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "render3d.h"
#include <raylib.h>
#include <rlgl.h>
#include <string>

namespace njin {
namespace {
constexpr const char *look_vs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 v_world;
out vec2 v_uv;
out vec4 v_color;
void main() {
  v_world = vertexPosition;
  v_uv = vertexTexCoord;
  v_color = vertexColor;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

#define NJIN_NOISE3 R"(
float hash(vec3 p) {
  p = fract(p * 0.3183099 + 0.1);
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),
                 mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
                 mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
)"

// The plain look (particle3d_flat) for the HDR image: the image times the
// colour, as raylib's own shader draws it.
constexpr const char *flat_fs = R"(#version 330
in vec3 v_world;
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
out vec4 finalColor;
void main() {
  finalColor = texture(texture0, v_uv) * colDiffuse * v_color;
}
)";

// Appended to every look: the colour into linear light for the HDR image.
constexpr const char *hdr_wrap = R"(
uniform int hdrOut;
void main() {
  look_main();
  if (hdrOut == 1) {
    vec3 c = max(finalColor.rgb, vec3(0.0));
    finalColor.rgb = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
  }
}
)";

// Smoke: a soft ball eaten at its edge by fractal noise rising through the
// world; lit on the side toward the sun, the rest in the ambient light.
constexpr const char *smoke_fs = R"(#version 330
in vec3 v_world;
in vec2 v_uv;
in vec4 v_color;
uniform float time;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambient;
out vec4 finalColor;
)" NJIN_NOISE3 R"(
// `pix` is how many noise cells of the first octave one pixel covers: an
// octave finer than a pixel only adds grain and shimmer, so it fades to its mean.
float fbm(vec3 p, float pix) {
  float v = 0.0, a = 0.5;
  for (int i = 0; i < 4; i++) {
    v += a * mix(noise(p), 0.5, smoothstep(0.25, 0.5, pix));
    p = p * 2.03 + vec3(1.7, 9.2, 3.1);
    a *= 0.5;
    pix *= 2.03;
  }
  return v;
}
void main() {
  vec2 c = v_uv * 2.0 - 1.0;
  float r = dot(c, c);
  if (r >= 1.0) discard;
  vec3 n = vec3(c.x, -c.y, sqrt(1.0 - r));
  // The quad's side in world units, from how fast world and image coordinates
  // change across a pixel. Small puffs keep the noise at its world scale; a
  // big one gets at most a few cells across, so it stays a soft cloud.
  float side = length(dFdx(v_world)) / max(length(dFdx(v_uv)), 1e-6);
  float freq = min(2.2, 3.0 / max(side, 1e-3));
  vec3 p = v_world * freq + vec3(0.0, -time * 0.9, time * 0.25);
  float d = fbm(p + vec3(c * 0.6, 0.0), length(fwidth(v_world)) * freq);
  float age = 1.0 - v_color.a;
  float body = smoothstep(0.0, 0.55, 1.0 - r) * smoothstep(0.18 + 0.35 * age, 0.75, d + 0.25 * (1.0 - r));
  if (body <= 0.003) discard;
  float lit = clamp(dot(n, normalize(vec3(lightDir.x * 0.4, max(lightDir.y, 0.3), 0.6))), 0.0, 1.0);
  lit = mix(lit, 1.0, 0.25) * mix(0.75, 1.1, d);
  vec3 shade = clamp(ambient * 1.4, 0.0, 1.0);
  vec3 sun = clamp(lightColor * 0.4 + 0.6, 0.0, 1.2);
  finalColor = vec4(v_color.rgb * mix(shade, sun, lit), body * min(v_color.a * 1.6, 1.0));
}
)";

// Fire: white-hot core through yellow and orange to a red fringe, flickering.
constexpr const char *fire_fs = R"(#version 330
in vec3 v_world;
in vec2 v_uv;
in vec4 v_color;
uniform float time;
out vec4 finalColor;
)" NJIN_NOISE3 R"(
void main() {
  vec2 c = v_uv * 2.0 - 1.0;
  float r = length(c);
  if (r >= 1.0) discard;
  float flicker = noise(v_world * 7.0 + vec3(0.0, time * 9.0, time * 3.0));
  float heat = clamp((1.0 - r) * (0.6 + 0.8 * flicker) * (0.4 + v_color.a), 0.0, 1.0);
  vec3 colour = mix(vec3(0.6, 0.08, 0.02), vec3(1.0, 0.55, 0.12), smoothstep(0.1, 0.5, heat));
  colour = mix(colour, vec3(1.0, 0.95, 0.75), smoothstep(0.55, 0.95, heat));
  finalColor = vec4(colour, smoothstep(0.0, 0.35, heat) * v_color.a * 0.45);
}
)";

// Sparks, as streaks: u across, v from the head (0) to the tail (1).
constexpr const char *spark_fs = R"(#version 330
in vec2 v_uv;
in vec4 v_color;
out vec4 finalColor;
void main() {
  float across = abs(v_uv.x * 2.0 - 1.0);
  float core = exp(-across * across * 18.0);
  float glow = exp(-across * across * 3.0);
  float tail = pow(1.0 - v_uv.y, 1.6);
  float cool = 1.0 - v_color.a;
  vec3 hot = mix(vec3(1.0, 0.95, 0.8), vec3(1.0, 0.55, 0.15), cool);
  vec3 edge = mix(vec3(1.0, 0.45, 0.08), vec3(0.7, 0.12, 0.02), cool);
  vec3 colour = (hot * core + edge * glow * 0.6) * tail;
  finalColor = vec4(colour, clamp(v_color.a * (glow + core) * tail, 0.0, 1.0));
}
)";

// A jet's flame, of fast streaks at one speed: age is distance from the
// nozzle. Shock diamonds at fixed ages; red is the thrust.
constexpr const char *flame_fs = R"(#version 330
in vec3 v_world;
in vec2 v_uv;
in vec4 v_color;
uniform float time;
out vec4 finalColor;
)" NJIN_NOISE3 R"(
void main() {
  float thrust = 0.4 + 0.6 * sqrt(v_color.r);
  float age = 1.0 - v_color.a;
  float across = abs(v_uv.x * 2.0 - 1.0);
  float body = exp(-across * across * 3.0) * smoothstep(0.0, 0.15, v_uv.y) * smoothstep(1.0, 0.75, v_uv.y);
  float band = pow(0.5 + 0.5 * cos(age * 6.2831853 * 4.0), 6.0) * smoothstep(0.75, 0.1, age);
  float tear = noise(v_world * 6.0 + vec3(0.0, -time * 14.0, time * 5.0));
  float heat = (1.0 - age) * (0.85 + 0.3 * tear);
  vec3 colour = mix(vec3(0.55, 0.08, 0.02), vec3(1.0, 0.42, 0.08), smoothstep(0.05, 0.35, heat));
  colour = mix(colour, vec3(1.0, 0.82, 0.45), smoothstep(0.35, 0.7, heat));
  colour = mix(colour, vec3(0.78, 0.86, 1.0), smoothstep(0.75, 0.97, heat));
  float light = body * thrust * (0.35 + 0.65 * pow(1.0 - age, 0.7)) * (0.8 + 0.9 * band) * (0.7 + 0.6 * tear);
  finalColor = vec4(colour, clamp(light * 0.45, 0.0, 1.0));
}
)";
#undef NJIN_NOISE3

// Ribbons (draw_ribbon3d): `v_uv.x` runs across the ribbon, 0 to 1. The
// colour carries the strength in its alpha, so both blends read it the same.
// Flat: the colour all across, its edges a little soft.
constexpr const char *ribbon_flat_fs = R"(#version 330
in vec3 v_world;
in vec2 v_uv;
in vec4 v_color;
uniform float intensity;
out vec4 finalColor;
void main() {
  float x = abs(v_uv.x * 2.0 - 1.0);
  finalColor = vec4(v_color.rgb * intensity, v_color.a * smoothstep(1.0, 0.75, x));
}
)";

// Glow: a narrow white-hot core in a soft halo of the colour, falling to
// nothing at the edges, so no edge of the quad shows.
constexpr const char *ribbon_glow_fs = R"(#version 330
in vec3 v_world;
in vec2 v_uv;
in vec4 v_color;
uniform float intensity;
out vec4 finalColor;
void main() {
  float x = v_uv.x * 2.0 - 1.0;
  float halo = max(exp(-4.5 * x * x) - 0.011, 0.0) / 0.989;
  float core = exp(-60.0 * x * x);
  float hot = core * clamp(v_color.a * 1.5, 0.0, 1.0);
  vec3 colour = mix(v_color.rgb, vec3(1.0, 0.98, 0.94), hot) * intensity;
  finalColor = vec4(colour, clamp(halo * 0.55 + core, 0.0, 1.0) * v_color.a);
}
)";
} // namespace

particle3d_looks::~particle3d_looks() {
  for (Shader &s : shaders)
    if (s.id != 0 && IsShaderValid(s))
      UnloadShader(s);
  for (Shader &s : ribbons)
    if (s.id != 0 && IsShaderValid(s))
      UnloadShader(s);
}

namespace {
// Compiles a look's fragment shader with look_vs: its main() becomes
// look_main(), wrapped by one that takes its colour into linear light for the
// HDR image (light3d::hdr). An empty shader when it does not compile.
Shader compile_look(const char *source) {
  std::string fs = source;
  const usize at = fs.find("void main()");
  if (at != std::string::npos)
    fs.replace(at, 11, "void look_main()");
  fs += hdr_wrap;
  Shader s = LoadShaderFromMemory(look_vs, fs.c_str());
  return IsShaderValid(s) && s.id != rlGetShaderIdDefault() ? s : Shader{};
}

void set_hdr_out(context &ctx, const Shader &s) {
  const i32 hdr_out = ctx_ref<render3d_state>(ctx)->hdr_out ? 1 : 0;
  const i32 hdr_loc = GetShaderLocation(s, "hdrOut");
  if (hdr_loc >= 0)
    SetShaderValue(s, hdr_loc, &hdr_out, SHADER_UNIFORM_INT);
}
} // namespace

bool ribbon3d_look_begin(context &ctx, particle3d_looks &looks, ribbon3d_style style, f32 intensity) {
  const i32 i = (i32)style;
  if (i < 0 || i >= ribbon3d_look_count)
    return false;
  if (!looks.ribbon_tried[i]) {
    looks.ribbon_tried[i] = true;
    static const char *const sources[ribbon3d_look_count] = {ribbon_flat_fs, ribbon_glow_fs};
    looks.ribbons[i] = compile_look(sources[i]);
  }
  const Shader &s = looks.ribbons[i];
  if (s.id == 0)
    return false;
  BeginShaderMode(s);
  set_hdr_out(ctx, s);
  const i32 loc = GetShaderLocation(s, "intensity");
  if (loc >= 0)
    SetShaderValue(s, loc, &intensity, SHADER_UNIFORM_FLOAT);
  return true;
}

bool particle3d_look_begin(context &ctx, particle3d_looks &looks, particle3d_style style) {
  const i32 i = (i32)style;
  // The plain look only for the HDR image (as raylib's shader otherwise).
  const bool hdr = ctx_ref<render3d_state>(ctx)->hdr_out;
  if (i < (i32)particle3d_flat || i >= particle3d_look_count || (i == (i32)particle3d_flat && !hdr))
    return false;
  if (!looks.tried[i]) {
    looks.tried[i] = true;
    static const char *const sources[particle3d_look_count] = {flat_fs, smoke_fs, fire_fs, spark_fs, flame_fs};
    looks.shaders[i] = compile_look(sources[i]);
  }
  const Shader &s = looks.shaders[i];
  if (s.id == 0)
    return false;
  BeginShaderMode(s);
  set_hdr_out(ctx, s);
  const light3d &sun = ctx_ref<render3d_state>(ctx)->light;
  const f32 time = elapsed(ctx);
  const vec3 toward = length_sq(sun.direction) > 0.0f ? normalize(sun.direction) * -1.0f : vec3{0.0f, 1.0f, 0.0f};
  const f32 light_dir[3] = {toward.x, toward.y, toward.z};
  const f32 light_color[3] = {sun.color.r, sun.color.g, sun.color.b};
  const f32 ambient[3] = {sun.ambient.r, sun.ambient.g, sun.ambient.b};
  const auto set = [&](const char *name, const void *value, i32 type) {
    const i32 loc = GetShaderLocation(s, name);
    if (loc >= 0)
      SetShaderValue(s, loc, value, type);
  };
  set("time", &time, SHADER_UNIFORM_FLOAT);
  set("lightDir", light_dir, SHADER_UNIFORM_VEC3);
  set("lightColor", light_color, SHADER_UNIFORM_VEC3);
  set("ambient", ambient, SHADER_UNIFORM_VEC3);
  return true;
}
} // namespace njin
