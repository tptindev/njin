// Reflection probes (probe3d_add) and the irradiance volume
// (irradiance3d_set); render3d_ext.h.
//
// A probe is captured by end_3d from its pass's draws: six 90 degree faces
// side by side (render3d.cpp's capture_faces, linear light), turned here into
// an equirectangular image, made into maps as light3d::environment's are
// (render3d_env.cpp: spherical harmonics and a GGX-prefiltered image per
// roughness), and put in its column of the probe atlas (its harmonics in a
// row of probeSH). The lighting shaders blend the probes whose box holds the
// point, the reflection turned to where it meets the box when asked.
//
// The irradiance volume captures each grid point the same way at 16 pixels a
// face, read back: per point an ambient cube (the light from +x, -x, +y, -y,
// +z, -z, each the cosine-weighted sum over the faces, over pi), a column of
// irrTex. A few points a pass, so a large grid does not stall one frame.
#include "render3d.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_log.h"
#include "njin_log_impl.h"
#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <string>

namespace njin {
const vec3 cube_dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const vec3 cube_ups[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};

namespace {
constexpr i32 equi_w = 512, equi_h = 256;

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

// The six faces to an equirectangular image (as render3d_env.cpp reads one):
// each direction's face and where on it, by the faces' own frames.
const char *const equi_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec3 faceX[6];
uniform vec3 faceY[6];
uniform vec3 faceZ[6];
out vec4 finalColor;
const float PI = 3.14159265;
void main() {
  float phi = (fragTexCoord.x - 0.5) * 2.0 * PI, theta = fragTexCoord.y * PI;
  vec3 d = vec3(cos(phi) * sin(theta), cos(theta), sin(phi) * sin(theta));
  vec3 a = abs(d);
  int f = a.x >= a.y && a.x >= a.z ? (d.x > 0.0 ? 0 : 1) : a.y >= a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5);
  float z = -dot(d, faceZ[f]);
  vec2 ndc = vec2(dot(d, faceX[f]), dot(d, faceY[f])) / max(z, 1e-5);
  vec2 uv = clamp(ndc * 0.5 + 0.5, vec2(0.002), vec2(0.998));
  finalColor = vec4(texture(texture0, vec2((float(f) + uv.x) / 6.0, uv.y)).rgb, 1.0);
}
)";

// A face's view frame as raylib's MatrixLookAt makes it: x right, y up, z back.
void face_frame(i32 f, vec3 &x, vec3 &y, vec3 &z) {
  z = cube_dirs[f] * -1.0f;
  x = normalize(cross(cube_ups[f], z));
  y = cross(z, x);
}

void free_capture(u32 &fbo, u32 &color, u32 &depth) {
  if (fbo != 0)
    rlUnloadFramebuffer(fbo);
  if (color != 0)
    rlUnloadTexture(color);
  if (depth != 0)
    rlUnloadTexture(depth);
  fbo = color = depth = 0;
}

bool make_shaders(probe_state &p) {
  if (p.tried)
    return p.ok;
  p.tried = true;
  p.equi = LoadShaderFromMemory(quad_vs, equi_fs);
  p.ok = IsShaderValid(p.equi);
  if (!p.ok)
    NJIN_WARN("3d: the probe shader failed to compile; probe3d_add() and irradiance3d_set() are not used");
  return p.ok;
}

probe_slot *slot_of(probe_state &p, probe3d_handle h) {
  if (h.id == 0 || h.id > p.slots.size())
    return nullptr;
  probe_slot &s = p.slots[h.id - 1];
  return s.alive && s.gen == h.gen ? &s : nullptr;
}

void set_v4(Shader s, const char *name, vec4 v) {
  const i32 loc = GetShaderLocation(s, name);
  if (loc >= 0)
    SetShaderValue(s, loc, &v, SHADER_UNIFORM_VEC4);
}
void set_i(Shader s, const char *name, i32 v) {
  const i32 loc = GetShaderLocation(s, name);
  if (loc >= 0)
    SetShaderValue(s, loc, &v, SHADER_UNIFORM_INT);
}
} // namespace

bool probe_capture_target(probe_state &p, i32 size, bool small) {
  u32 &fbo = small ? p.small_fbo : p.cap_fbo;
  u32 &color = small ? p.small_color : p.cap_color;
  u32 &depth = small ? p.small_depth : p.cap_depth;
  i32 &have = small ? p.small_size : p.cap_size;
  if (fbo != 0 && have == size)
    return true;
  free_capture(fbo, color, depth);
  have = 0;
  // The grid's faces are read back as floats; a probe's are only sampled.
  color = rlLoadTexture(nullptr, size * 6, size,
                        small ? RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32 : RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
  rlTextureParameters(color, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
  rlTextureParameters(color, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
  rlTextureParameters(color, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
  rlTextureParameters(color, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
  depth = rlLoadTextureDepth(size * 6, size, false);
  fbo = rlLoadFramebuffer();
  rlFramebufferAttach(fbo, color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  rlFramebufferAttach(fbo, depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
  if (color == 0 || depth == 0 || !rlFramebufferComplete(fbo)) {
    free_capture(fbo, color, depth);
    NJIN_WARN_LIMITED("3d: could not make a probe capture target (float framebuffer)");
    return false;
  }
  have = size;
  return true;
}

bool probe_store(render3d_state &s, i32 index) {
  probe_state &p = s.probes;
  if (!make_shaders(p))
    return false;
  // 1. The faces to an equirectangular image.
  if (p.equi_tex == 0 && !make_float_target(equi_w, equi_h, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, p.equi_tex, p.equi_fbo))
    return false;
  vec3 fx[6], fy[6], fz[6];
  for (i32 f = 0; f < 6; f++)
    face_frame(f, fx[f], fy[f], fz[f]);
  SetShaderValueV(p.equi, GetShaderLocation(p.equi, "faceX"), fx, SHADER_UNIFORM_VEC3, 6);
  SetShaderValueV(p.equi, GetShaderLocation(p.equi, "faceY"), fy, SHADER_UNIFORM_VEC3, 6);
  SetShaderValueV(p.equi, GetShaderLocation(p.equi, "faceZ"), fz, SHADER_UNIFORM_VEC3, 6);
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(p.equi_fbo);
  rlSetFramebufferWidth(equi_w);
  rlSetFramebufferHeight(equi_h);
  rlViewport(0, 0, equi_w, equi_h);
  rlDisableDepthTest();
  render3d_quad(p.equi, p.cap_color);
  rlDisableFramebuffer();
  // 2. Its maps, at a probe's sizes.
  const Texture2D equi{.id = p.equi_tex, .width = equi_w, .height = equi_h, .mipmaps = 1,
                       .format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16};
  if (!env_build(p.builder, equi, equi_w, equi_h, probe_column, probe_level) || p.builder.levels != probe_levels)
    return false;
  // 3. Into the atlas column and the harmonics row.
  if (p.atlas == 0) {
    if (!make_float_target(probe_column * probe3d_max, probe_level * probe_levels, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16,
                           p.atlas, p.atlas_fbo))
      return false;
    p.sh_tex = rlLoadTexture(nullptr, 9, probe3d_max, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
    rlTextureParameters(p.sh_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(p.sh_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
  }
  const u32 from = rlLoadFramebuffer();
  rlFramebufferAttach(from, p.builder.atlas.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  const bool ok = rlFramebufferComplete(from);
  if (ok) {
    rlDrawRenderBatchActive();
    rlBindFramebuffer(RL_READ_FRAMEBUFFER, from);
    rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, p.atlas_fbo);
    rlBlitFramebuffer(0, 0, probe_column, probe_level * probe_levels, index * probe_column, 0, probe_column,
                      probe_level * probe_levels, 0x00004000);
    rlDisableFramebuffer();
  }
  rlUnloadFramebuffer(from);
  if (!ok)
    return false;
  f32 sh[9 * 4]{};
  for (i32 k = 0; k < 9; k++) {
    p.sh[(usize)index][(usize)k] = p.builder.sh[k];
    sh[k * 4] = p.builder.sh[k].x;
    sh[k * 4 + 1] = p.builder.sh[k].y;
    sh[k * 4 + 2] = p.builder.sh[k].z;
  }
  rlUpdateTexture(p.sh_tex, 0, index, 9, 1, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, sh);
  return true;
}

void grid_store(probe_state &p, i32 point, i32 size) {
  const irradiance3d_desc &g = p.grid;
  const i32 total = std::clamp(g.count_x, 1, 16) * std::clamp(g.count_y, 1, 16) * std::clamp(g.count_z, 1, 16);
  if ((i32)p.grid_data.size() != total * 6 * 4)
    p.grid_data.assign((usize)total * 6 * 4, 0.0f);
  f32 *px = (f32 *)rlReadTexturePixels(p.small_color, size * 6, size, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
  if (px == nullptr)
    return;
  static const vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  vec3 sum[6]{};
  for (i32 f = 0; f < 6; f++) {
    vec3 fx, fy, fz;
    face_frame(f, fx, fy, fz);
    for (i32 j = 0; j < size; j++)
      for (i32 i = 0; i < size; i++) {
        // Texture row j is the face's y = (j + 0.5) / size from the bottom.
        const f32 x = ((f32)i + 0.5f) / (f32)size * 2.0f - 1.0f;
        const f32 y = ((f32)j + 0.5f) / (f32)size * 2.0f - 1.0f;
        const vec3 d = normalize(fx * x + fy * y - fz);
        const f32 solid = (4.0f / (f32)(size * size)) / std::pow(1.0f + x * x + y * y, 1.5f);
        const f32 *c = px + (((usize)j * (usize)(size * 6)) + (usize)(f * size + i)) * 4;
        const vec3 radiance{c[0], c[1], c[2]};
        for (i32 a = 0; a < 6; a++) {
          const f32 k = dot(d, axes[a]);
          if (k > 0.0f)
            sum[a] = sum[a] + radiance * (k * solid);
        }
      }
  }
  MemFree(px);
  // Irradiance over pi: what a white Lambertian face shows, the ambient
  // colour's meaning.
  for (i32 a = 0; a < 6; a++) {
    f32 *t = &p.grid_data[((usize)a * (usize)total + (usize)point) * 4];
    t[0] = sum[a].x / PI, t[1] = sum[a].y / PI, t[2] = sum[a].z / PI, t[3] = 1.0f;
  }
  if (point + 1 >= total) {
    if (p.grid_tex != 0)
      rlUnloadTexture(p.grid_tex);
    p.grid_tex = rlLoadTexture(p.grid_data.data(), total, 6, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
    rlTextureParameters(p.grid_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(p.grid_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    p.grid_ready = p.grid_tex != 0;
  }
}

void probes_free(probe_state &p) {
  free_capture(p.cap_fbo, p.cap_color, p.cap_depth);
  free_capture(p.small_fbo, p.small_color, p.small_depth);
  free_float_target(p.atlas, p.atlas_fbo);
  free_float_target(p.equi_tex, p.equi_fbo);
  for (u32 *id : {&p.sh_tex, &p.grid_tex})
    if (*id != 0) {
      rlUnloadTexture(*id);
      *id = 0;
    }
  env_free(p.builder);
  if (IsShaderValid(p.equi))
    UnloadShader(p.equi);
  p.equi = Shader{};
  p.slots.clear();
}

void probes_uniforms(const render3d_state &s, Shader sh, bool capturing) {
  const probe_state &p = s.probes;
  i32 count = 0;
  vec4 boxes[probe3d_max * 2]{};
  vec4 at[probe3d_max]{};
  // A shader takes the probes by their atlas column: one past the last ready one.
  if (!capturing && p.atlas != 0)
    for (usize i = 0; i < p.slots.size() && i < (usize)probe3d_max; i++) {
      const probe_slot &slot = p.slots[i];
      const probe3d_desc &d = slot.desc;
      if (slot.alive && slot.ready) {
        boxes[i * 2] = {d.position.x, d.position.y, d.position.z, std::max(d.blend, 0.01f)};
        boxes[i * 2 + 1] = {std::fabs(d.size.x) * 0.5f, std::fabs(d.size.y) * 0.5f, std::fabs(d.size.z) * 0.5f,
                            std::max(d.intensity, 0.0f)};
        at[i] = {d.position.x, d.position.y, d.position.z, d.parallax ? 1.0f : 0.0f};
        count = (i32)i + 1;
      } else {
        // An empty column: a box no point is inside.
        boxes[i * 2] = {0.0f, -1e9f, 0.0f, 1.0f};
        boxes[i * 2 + 1] = {0.0f, 0.0f, 0.0f, 0.0f};
      }
    }
  set_i(sh, "probeCount", count);
  if (count > 0) {
    SetShaderValueV(sh, GetShaderLocation(sh, "probeBox"), boxes, SHADER_UNIFORM_VEC4, count * 2);
    SetShaderValueV(sh, GetShaderLocation(sh, "probeAt"), at, SHADER_UNIFORM_VEC4, count);
    set_i(sh, "probeAtlas", probe_atlas_unit);
    set_i(sh, "probeSH", probe_sh_unit);
    set_v4(sh, "probeParams", {(f32)probe_levels, 0.5f / (f32)probe_level, (f32)probe3d_max, 0.5f / (f32)probe_column});
  }
  const bool grid = !capturing && p.grid_ready && p.grid_set;
  set_i(sh, "irrOn", grid ? 1 : 0);
  if (grid) {
    const irradiance3d_desc &g = p.grid;
    const vec3 size{std::max(std::fabs(g.size.x), 1e-3f), std::max(std::fabs(g.size.y), 1e-3f), std::max(std::fabs(g.size.z), 1e-3f)};
    const vec3 lo = g.center - size * 0.5f;
    set_v4(sh, "irrMin", {lo.x, lo.y, lo.z, std::max(g.blend, 0.01f)});
    set_v4(sh, "irrSize", {size.x, size.y, size.z, std::max(g.intensity, 0.0f)});
    set_v4(sh, "irrCount", {(f32)std::clamp(g.count_x, 1, 16), (f32)std::clamp(g.count_y, 1, 16),
                             (f32)std::clamp(g.count_z, 1, 16), 0.0f});
    set_i(sh, "irrTex", grid_tex_unit);
  }
}

void probes_bind(const render3d_state &s) {
  const probe_state &p = s.probes;
  if (p.atlas != 0) {
    rlActiveTextureSlot(probe_atlas_unit);
    rlEnableTexture(p.atlas);
    rlActiveTextureSlot(probe_sh_unit);
    rlEnableTexture(p.sh_tex);
  }
  if (p.grid_ready) {
    rlActiveTextureSlot(grid_tex_unit);
    rlEnableTexture(p.grid_tex);
  }
  rlActiveTextureSlot(0);
}

probe3d_handle probe3d_add(context &ctx, const probe3d_desc &desc) {
  probe_state &p = ctx_ref<render3d_state>(ctx)->probes;
  usize slot = p.slots.size();
  for (usize i = 0; i < p.slots.size(); i++)
    if (!p.slots[i].alive) {
      slot = i;
      break;
    }
  if (slot >= (usize)probe3d_max) {
    NJIN_WARN_LIMITED("probe3d_add: more than %d probes, ignored", probe3d_max);
    return {};
  }
  if (slot == p.slots.size())
    p.slots.emplace_back();
  probe_slot &s = p.slots[slot];
  s.desc = desc;
  s.gen++;
  s.alive = true;
  s.dirty = true;
  s.ready = false;
  return probe3d_handle{(u32)slot + 1, s.gen};
}

void probe3d_set(context &ctx, probe3d_handle handle, const probe3d_desc &desc) {
  if (probe_slot *s = slot_of(ctx_ref<render3d_state>(ctx)->probes, handle)) {
    s->desc = desc;
    s->dirty = true;
  }
}

void probe3d_refresh(context &ctx, probe3d_handle handle) {
  if (probe_slot *s = slot_of(ctx_ref<render3d_state>(ctx)->probes, handle))
    s->dirty = true;
}

void probe3d_remove(context &ctx, probe3d_handle handle) {
  if (probe_slot *s = slot_of(ctx_ref<render3d_state>(ctx)->probes, handle)) {
    s->alive = false;
    s->ready = false;
  }
}

void irradiance3d_set(context &ctx, const irradiance3d_desc &desc) {
  probe_state &p = ctx_ref<render3d_state>(ctx)->probes;
  p.grid = desc;
  p.grid_set = true;
  p.grid_ready = false;
  p.grid_next = 0;
  p.grid_data.clear();
}

void irradiance3d_refresh(context &ctx) {
  probe_state &p = ctx_ref<render3d_state>(ctx)->probes;
  if (!p.grid_set)
    return;
  // The old grid lights the scene until the new one is complete.
  p.grid_next = 0;
  p.grid_data.clear();
}

void irradiance3d_clear(context &ctx) {
  probe_state &p = ctx_ref<render3d_state>(ctx)->probes;
  p.grid_set = false;
  p.grid_ready = false;
  p.grid_next = 0;
}
} // namespace njin
