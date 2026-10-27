#include "render3d_env.h"
#include "render3d.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_log.h"
#include "njin_texture.h"
#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <string>

namespace njin {
namespace {
// The sizes for light3d::environment (a probe's are smaller, render3d_probe.cpp).
constexpr i32 env_base_width = 1024, env_base_height = 512;
constexpr i32 env_atlas_width = 512, env_level_height = 256;
constexpr i32 sh_width = 64, sh_height = 32;

// A full-screen quad, the texture coordinate 0 at the bottom-left: the row a
// fragment writes is the image's row at that coordinate.
const char *const quad_vs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 fragTexCoord;
void main() {
  fragTexCoord = vertexTexCoord;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// The game's image to linear light: an 8-bit one is sRGB.
const char *const copy_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform int decode;
uniform float lod;
out vec4 finalColor;
void main() {
  vec3 c = textureLod(texture0, fragTexCoord, lod).rgb;
  if (decode == 1)
    c = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
  finalColor = vec4(c, 1.0);
}
)";

// One level of the atlas: the radiance seen along each direction by a
// surface of `roughness`, GGX importance sampled (Karis, "Real Shading in
// Unreal Engine 4"), each sample read from the mip whose texel covers about
// its share of the lobe so few samples do not alias.
const char *const prefilter_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform float roughness;
uniform float sourceWidth;
out vec4 finalColor;
const float PI = 3.14159265;
const uint SAMPLES = 256u;
vec3 dir_of(vec2 uv) {
  float phi = (uv.x - 0.5) * 2.0 * PI, theta = uv.y * PI;
  return vec3(cos(phi) * sin(theta), cos(theta), sin(phi) * sin(theta));
}
vec2 uv_of(vec3 d) {
  return vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
}
float radical_inverse(uint b) {
  b = (b << 16u) | (b >> 16u);
  b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
  b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
  b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
  b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
  return float(b) * 2.3283064365386963e-10;
}
void main() {
  vec3 n = dir_of(fragTexCoord);
  if (roughness <= 0.0) {
    finalColor = vec4(textureLod(texture0, fragTexCoord, 0.0).rgb, 1.0);
    return;
  }
  vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 tx = normalize(cross(up, n));
  vec3 ty = cross(n, tx);
  float a = roughness * roughness;
  // Solid angle of one source texel, on average.
  float texel = 4.0 * PI / (sourceWidth * sourceWidth * 0.5);
  vec3 sum = vec3(0.0);
  float weight = 0.0;
  for (uint i = 0u; i < SAMPLES; i++) {
    vec2 xi = vec2(float(i) / float(SAMPLES), radical_inverse(i));
    float phi = 2.0 * PI * xi.x;
    float ct = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float st = sqrt(1.0 - ct * ct);
    vec3 h = tx * (st * cos(phi)) + ty * (st * sin(phi)) + n * ct;
    vec3 l = normalize(2.0 * dot(n, h) * h - n);
    float nl = dot(n, l);
    if (nl <= 0.0)
      continue;
    float nh = max(dot(n, h), 0.0);
    float q = nh * nh * (a * a - 1.0) + 1.0;
    float d = a * a / (PI * q * q);
    float pdf = d / 4.0 + 1e-4; // n = v: nh / (4 vh) = 1/4
    float sample_angle = 1.0 / (float(SAMPLES) * pdf);
    float lod = max(0.5 * log2(sample_angle / texel) + 1.0, 0.0);
    sum += textureLod(texture0, uv_of(l), lod).rgb * nl;
    weight += nl;
  }
  finalColor = vec4(sum / max(weight, 1e-4), 1.0);
}
)";

// The environment behind the scene: the direction of each pixel through the
// inverse view-projection, the base image there, tone mapped like the lit
// shaders (render3d's lighting GLSL is prepended for tone_out and env_dir).
const char *const sky_fs_main = R"(
in vec2 fragTexCoord;
uniform sampler2D skyMap;
uniform mat4 skyInvViewProj;
uniform vec3 skyEye;
void main() {
  vec4 far = skyInvViewProj * vec4(fragTexCoord * 2.0 - 1.0, 1.0, 1.0);
  vec4 near = skyInvViewProj * vec4(fragTexCoord * 2.0 - 1.0, -1.0, 1.0);
  vec3 d = normalize(far.xyz / far.w - near.xyz / near.w);
  d = env_dir(d);
  vec2 uv = vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
  vec3 c = textureLod(skyMap, uv, 0.0).rgb * envParams.y;
  if (hdrOut == 1)
    finalColor = vec4(c * exposure, 1.0); // into the HDR image, tone mapped at the pass's end
  else
    finalColor = vec4(toneMap > 0 ? tone_out(c) : linear_to_srgb(clamp(c, 0.0, 1.0)), 1.0);
}
)";

f32 to_linear(f32 c) {
  c = std::max(c, 0.0f);
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Draws a full-screen quad with `shader` into the bound framebuffer's viewport,
// `texture` on unit 0, with identity matrices.
void draw_quad(const Shader &shader, u32 texture) {
  rlDrawRenderBatchActive();
  // Saved and set, not pushed: rlPushMatrix in modelview mode switches to a
  // CPU-side transform and leaves the pass's view in place.
  const Matrix projection = rlGetMatrixProjection();
  const Matrix modelview = rlGetMatrixModelview();
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  BeginShaderMode(shader);
  rlSetTexture(texture);
  rlBegin(RL_QUADS);
  rlColor4ub(255, 255, 255, 255);
  rlTexCoord2f(0.0f, 0.0f);
  rlVertex2f(-1.0f, -1.0f);
  rlTexCoord2f(1.0f, 0.0f);
  rlVertex2f(1.0f, -1.0f);
  rlTexCoord2f(1.0f, 1.0f);
  rlVertex2f(1.0f, 1.0f);
  rlTexCoord2f(0.0f, 1.0f);
  rlVertex2f(-1.0f, 1.0f);
  rlEnd();
  rlSetTexture(0);
  rlDrawRenderBatchActive();
  EndShaderMode();
  rlSetMatrixProjection(projection);
  rlSetMatrixModelview(modelview);
}

// A float texture of `format` with a framebuffer drawing into it.
bool float_target(i32 w, i32 h, i32 format, i32 mipmaps, Texture2D &tex, u32 &fbo) {
  tex = Texture2D{};
  tex.id = rlLoadTexture(nullptr, w, h, format, mipmaps);
  tex.width = w;
  tex.height = h;
  tex.mipmaps = mipmaps;
  tex.format = format;
  fbo = rlLoadFramebuffer();
  if (tex.id == 0 || fbo == 0)
    return false;
  rlFramebufferAttach(fbo, tex.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  return rlFramebufferComplete(fbo);
}

void bind_target(u32 fbo, i32 x, i32 y, i32 w, i32 h, i32 fw, i32 fh) {
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(fbo);
  rlSetFramebufferWidth(fw);
  rlSetFramebufferHeight(fh);
  rlViewport(x, y, w, h);
}

bool make_shaders(env_maps &env) {
  if (env.shaders_tried)
    return IsShaderValid(env.copy) && IsShaderValid(env.prefilter) && IsShaderValid(env.sky);
  env.shaders_tried = true;
  env.copy = LoadShaderFromMemory(quad_vs, copy_fs);
  env.prefilter = LoadShaderFromMemory(quad_vs, prefilter_fs);
  const std::string sky = std::string("#version 330\n") + render3d_lighting_glsl() + sky_fs_main;
  env.sky = LoadShaderFromMemory(quad_vs, sky.c_str());
  if (!IsShaderValid(env.copy) || !IsShaderValid(env.prefilter) || !IsShaderValid(env.sky)) {
    NJIN_WARN("3d: the environment shaders failed to compile; light3d::environment is not used");
    return false;
  }
  env.sky_env_params = GetShaderLocation(env.sky, "envParams");
  env.sky_tone = GetShaderLocation(env.sky, "toneMap");
  env.sky_exposure = GetShaderLocation(env.sky, "exposure");
  env.sky_inv_vp = GetShaderLocation(env.sky, "skyInvViewProj");
  env.sky_eye = GetShaderLocation(env.sky, "skyEye");
  env.sky_hdr = GetShaderLocation(env.sky, "hdrOut");
  env.sky_map = GetShaderLocation(env.sky, "skyMap");
  return true;
}

void free_maps(env_maps &env) {
  if (env.base.id != 0)
    rlUnloadTexture(env.base.id);
  if (env.atlas.id != 0)
    rlUnloadTexture(env.atlas.id);
  env.base = Texture2D{};
  env.atlas = Texture2D{};
  env.ready = false;
}

// Spherical harmonics of the image's radiance, from a small copy read back.
void project_sh(env_maps &env, const f32 *rgba) {
  for (vec3 &c : env.sh)
    c = {};
  for (i32 y = 0; y < sh_height; y++) {
    // Texture row y holds the image's v = (y + 0.5) / height (see quad_vs).
    const f32 theta = ((f32)y + 0.5f) / (f32)sh_height * PI;
    const f32 area = (2.0f * PI / (f32)sh_width) * (PI / (f32)sh_height) * std::sin(theta);
    for (i32 x = 0; x < sh_width; x++) {
      const f32 phi = (((f32)x + 0.5f) / (f32)sh_width - 0.5f) * 2.0f * PI;
      const vec3 d{std::cos(phi) * std::sin(theta), std::cos(theta), std::sin(phi) * std::sin(theta)};
      const f32 *p = rgba + ((usize)y * sh_width + (usize)x) * 4;
      const vec3 c{p[0] * area, p[1] * area, p[2] * area};
      const f32 basis[9] = {0.282095f,
                            0.488603f * d.y,
                            0.488603f * d.z,
                            0.488603f * d.x,
                            1.092548f * d.x * d.y,
                            1.092548f * d.y * d.z,
                            0.315392f * (3.0f * d.z * d.z - 1.0f),
                            1.092548f * d.x * d.z,
                            0.546274f * (d.x * d.x - d.y * d.y)};
      for (i32 k = 0; k < 9; k++)
        env.sh[k] += c * basis[k];
    }
  }
}

bool make_maps(env_maps &env, const Texture2D &source, i32 base_width, i32 base_height, i32 atlas_width,
               i32 level_height) {
  if (!make_shaders(env))
    return false;
  free_maps(env);
  env.level_height = level_height;
  // 1. The image in linear light, mipmapped.
  u32 fbo = 0;
  bool ok = float_target(base_width, base_height, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1, env.base, fbo);
  if (ok) {
    bind_target(fbo, 0, 0, base_width, base_height, base_width, base_height);
    const i32 decode = source.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 ||
                               source.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8 ||
                               source.format == PIXELFORMAT_UNCOMPRESSED_GRAYSCALE ||
                               source.format == PIXELFORMAT_UNCOMPRESSED_GRAY_ALPHA
                           ? 1
                           : 0;
    const f32 lod = 0.0f;
    SetShaderValue(env.copy, GetShaderLocation(env.copy, "decode"), &decode, SHADER_UNIFORM_INT);
    SetShaderValue(env.copy, GetShaderLocation(env.copy, "lod"), &lod, SHADER_UNIFORM_FLOAT);
    draw_quad(env.copy, source.id);
  }
  rlDisableFramebuffer();
  if (fbo != 0)
    rlUnloadFramebuffer(fbo);
  if (!ok)
    return false;
  rlGenTextureMipmaps(env.base.id, env.base.width, env.base.height, env.base.format, &env.base.mipmaps);
  SetTextureFilter(env.base, TEXTURE_FILTER_TRILINEAR);
  SetTextureWrap(env.base, TEXTURE_WRAP_REPEAT);

  // 2. Its irradiance: a 64 x 32 copy (from a small mip, no aliasing) read back.
  Texture2D small{};
  fbo = 0;
  ok = float_target(sh_width, sh_height, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1, small, fbo);
  if (ok) {
    bind_target(fbo, 0, 0, sh_width, sh_height, sh_width, sh_height);
    const i32 decode = 0;
    const f32 lod = std::log2((f32)base_width / (f32)sh_width);
    SetShaderValue(env.copy, GetShaderLocation(env.copy, "decode"), &decode, SHADER_UNIFORM_INT);
    SetShaderValue(env.copy, GetShaderLocation(env.copy, "lod"), &lod, SHADER_UNIFORM_FLOAT);
    draw_quad(env.copy, env.base.id);
  }
  rlDisableFramebuffer();
  if (fbo != 0)
    rlUnloadFramebuffer(fbo);
  if (ok) {
    f32 *pixels = (f32 *)rlReadTexturePixels(small.id, sh_width, sh_height, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
    if (pixels != nullptr) {
      project_sh(env, pixels);
      // rlgl allocates through raylib's allocator (njin's counting one).
      MemFree(pixels);
    } else {
      ok = false;
    }
  }
  if (small.id != 0)
    rlUnloadTexture(small.id);
  if (!ok)
    return false;

  // 3. The prefiltered levels, stacked.
  const i32 h = env.level_height;
  fbo = 0;
  ok = float_target(atlas_width, h * env.levels, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1, env.atlas, fbo);
  if (ok) {
    const i32 rough_loc = GetShaderLocation(env.prefilter, "roughness");
    const i32 width_loc = GetShaderLocation(env.prefilter, "sourceWidth");
    const f32 width = (f32)base_width;
    SetShaderValue(env.prefilter, width_loc, &width, SHADER_UNIFORM_FLOAT);
    for (i32 k = 0; k < env.levels; k++) {
      bind_target(fbo, 0, k * h, atlas_width, h, atlas_width, h * env.levels);
      const f32 rough = (f32)k / (f32)(env.levels - 1);
      SetShaderValue(env.prefilter, rough_loc, &rough, SHADER_UNIFORM_FLOAT);
      draw_quad(env.prefilter, env.base.id);
    }
  }
  rlDisableFramebuffer();
  if (fbo != 0)
    rlUnloadFramebuffer(fbo);
  if (!ok)
    return false;
  SetTextureFilter(env.atlas, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(env.atlas, TEXTURE_WRAP_REPEAT);
  env.ready = true;
  return true;
}
} // namespace

bool env_build(env_maps &env, const Texture2D &source, i32 base_width, i32 base_height, i32 atlas_width,
               i32 level_height) {
  rlDrawRenderBatchActive();
  const bool ok = make_maps(env, source, base_width, base_height, atlas_width, level_height);
  rlDisableFramebuffer();
  return ok;
}

rgb_linear linear_of(rgba c) { return {to_linear(c.r), to_linear(c.g), to_linear(c.b)}; }

bool env_update(context &ctx, env_maps &env, const light3d &light) {
  const texture_slot *slot = texture_slot_of(ctx_ref<texture_store>(ctx), light.environment);
  if (slot == nullptr || slot->packed) {
    if (light.environment.id != 0 && !env.warned) {
      NJIN_WARN("3d: light3d::environment is not a loaded texture (nor one packed in an atlas)");
      env.warned = true;
    }
    if (env.source != 0)
      free_maps(env);
    env.source = 0;
    return false;
  }
  if (env.source == light.environment.id && env.version == slot->version)
    return false;
  env.source = light.environment.id;
  env.version = slot->version;
  env.warned = false;
  // Drawing below goes into the maps' own framebuffers.
  rlDrawRenderBatchActive();
  const bool ok = make_maps(env, slot->texture, env_base_width, env_base_height, env_atlas_width, env_level_height);
  rlDisableFramebuffer();
  if (!ok) {
    free_maps(env);
    NJIN_WARN("3d: could not make the environment's maps (float framebuffers); light3d::environment is not used");
  } else {
    NJIN_INFO("3d: environment ready (%d x %d image, %d levels), irradiance %.3f %.3f %.3f", slot->texture.width,
              slot->texture.height, env.levels, env.sh[0].x * 0.886227f / PI, env.sh[0].y * 0.886227f / PI,
              env.sh[0].z * 0.886227f / PI);
  }
  return true;
}

void env_set_uniforms(const env_maps &env, const light3d &light, Shader shader, i32 on, i32 sh, i32 map,
                      i32 params) {
  const i32 ready = env.ready ? 1 : 0;
  if (on >= 0)
    SetShaderValue(shader, on, &ready, SHADER_UNIFORM_INT);
  if (!env.ready)
    return;
  const f32 k = std::max(light.environment_intensity, 0.0f);
  vec3 scaled[9];
  for (i32 i = 0; i < 9; i++)
    scaled[i] = env.sh[i] * k;
  if (sh >= 0)
    SetShaderValueV(shader, sh, scaled, SHADER_UNIFORM_VEC3, 9);
  if (map >= 0)
    SetShaderValue(shader, map, &env_unit, SHADER_UNIFORM_INT);
  const vec4 p{(f32)env.levels, k, light.environment_rotation * DEG2RAD, 0.5f / (f32)env.level_height};
  if (params >= 0)
    SetShaderValue(shader, params, &p, SHADER_UNIFORM_VEC4);
}

void env_bind(const env_maps &env) {
  if (!env.ready)
    return;
  rlActiveTextureSlot(env_unit);
  rlEnableTexture(env.atlas.id);
}

void env_draw_sky(env_maps &env, const light3d &light, const camera3d &camera, const Matrix &view_proj, bool hdr_out,
                  f32 exposure) {
  if (!env.ready)
    return;
  rlDrawRenderBatchActive();
  rlDisableDepthTest();
  rlDisableDepthMask();
  const vec4 p{(f32)env.levels, std::max(light.environment_intensity, 0.0f), light.environment_rotation * DEG2RAD,
               0.5f / (f32)env.level_height};
  SetShaderValue(env.sky, env.sky_env_params, &p, SHADER_UNIFORM_VEC4);
  const i32 tone = light.tonemap == tonemap3d_none ? 0 : (i32)light.tonemap;
  SetShaderValue(env.sky, env.sky_tone, &tone, SHADER_UNIFORM_INT);
  SetShaderValue(env.sky, env.sky_exposure, &exposure, SHADER_UNIFORM_FLOAT);
  const i32 hdr = hdr_out ? 1 : 0;
  SetShaderValue(env.sky, env.sky_hdr, &hdr, SHADER_UNIFORM_INT);
  SetShaderValueMatrix(env.sky, env.sky_inv_vp, MatrixInvert(view_proj));
  SetShaderValue(env.sky, env.sky_eye, &camera.position, SHADER_UNIFORM_VEC3);
  const i32 unit = 0;
  SetShaderValue(env.sky, env.sky_map, &unit, SHADER_UNIFORM_INT);
  draw_quad(env.sky, env.base.id);
  rlEnableDepthMask();
  rlEnableDepthTest();
}

void env_free(env_maps &env) {
  free_maps(env);
  for (Shader *s : {&env.copy, &env.prefilter, &env.sky})
    if (IsShaderValid(*s))
      UnloadShader(*s);
  env = env_maps{};
}
} // namespace njin
