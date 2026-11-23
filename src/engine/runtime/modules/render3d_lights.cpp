// The extended lights (light3d::lights_max past light3d_max, or
// source_shadows past light3d_shadow_max; render3d_ext.h).
//
// Every light of the pass goes into lightTex (5 texels a row: position and
// reach, colour times intensity and the inner cone, direction and the outer
// cone, linear colour and shadow row; the fifth holds shadow row k's near,
// far, texel and 1 / tile when k < the rows), the lamps' face matrices into
// lampMatTex. The view is cut into 16 x 9 tiles and 24 depth slices (spread
// logarithmically, linearly for an orthographic camera); each light's sphere
// is put in the clusters its box on the screen and in depth touches, and the
// clusters' lists go into indexTex, their first index and count into
// clusterTex. The lighting shaders (render3d.cpp's lamp_range) look up their
// pixel's cluster and go through its list only. A capture (render3d's probes)
// goes through every light instead.
#include "render3d.h"
#include "njin_ctx.h"
#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

namespace njin {
namespace {
constexpr i32 light_texels = 5;
constexpr i32 index_width = 4096;

// A float texture of `format` grown to at least `w` x `h` rows (kept between
// passes), its contents replaced from `data`.
void upload(u32 &tex, i32 &rows, i32 w, i32 h, i32 format, const void *data) {
  if (tex == 0 || rows < h) {
    if (tex != 0)
      rlUnloadTexture(tex);
    rows = std::max(h, std::max(rows * 2, 16));
    tex = rlLoadTexture(nullptr, w, rows, format, 1);
    rlTextureParameters(tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
  }
  if (h > 0)
    rlUpdateTexture(tex, 0, 0, w, h, format, data);
}

void set_i(Shader s, const char *name, i32 v) {
  const i32 loc = GetShaderLocation(s, name);
  if (loc >= 0)
    SetShaderValue(s, loc, &v, SHADER_UNIFORM_INT);
}
void set_v4(Shader s, const char *name, vec4 v) {
  const i32 loc = GetShaderLocation(s, name);
  if (loc >= 0)
    SetShaderValue(s, loc, &v, SHADER_UNIFORM_VEC4);
}
} // namespace

void lights_ext_free(lights_ext &l) {
  for (u32 *id : {&l.light_tex, &l.cluster_tex, &l.index_tex, &l.lamp_mat_tex})
    if (*id != 0) {
      rlUnloadTexture(*id);
      *id = 0;
    }
  l.light_rows = l.index_rows = 0;
}

bool lights_ext_wanted(const light3d &light) {
  return light.lights_max > light3d_max || light.source_shadows > light3d_shadow_max;
}

void lights_ext_update(render3d_state &s, vec2 size) {
  lights_ext &l = s.lext;
  const i32 n = std::min((i32)s.lights.size(), light3d_limit);
  l.total = n;
  // The lights.
  l.light_data.assign((usize)std::max(n, light3d_shadow_limit) * light_texels * 4, 0.0f);
  for (i32 i = 0; i < n; i++) {
    const light3d_source &src = s.lights[(usize)i];
    const f32 k = src.intensity;
    const f32 half = clamp(src.cone, 0.0f, 179.0f) * 0.5f * DEG2RAD;
    const f32 inner = half * (1.0f - clamp(src.softness, 0.0f, 1.0f));
    const vec3 d = normalize(src.direction);
    const rgb_linear lc = linear_of(src.color);
    // A ray traced lamp reads its channel of the mask instead (-2, -3, -4).
    const i32 row = s.rt.wanted && s.rt.lamp[(usize)i] >= 0 ? -2 - s.rt.lamp[(usize)i] : s.lamps.row[(usize)i];
    const f32 v[16] = {src.position.x, src.position.y, src.position.z, std::max(src.radius, 0.001f),
                       src.color.r * k, src.color.g * k, src.color.b * k, std::cos(inner),
                       d.x, d.y, d.z, src.kind == light3d_spot ? std::cos(half) : -2.0f,
                       lc.r * k, lc.g * k, lc.b * k, (f32)row};
    std::copy(v, v + 16, &l.light_data[(usize)i * light_texels * 4]);
  }
  // The shadow rows' parameters, in the fifth texel of the row of that index.
  for (i32 r = 0; r < s.lamps.rows; r++) {
    const vec4 p = s.lamps.params[(usize)r];
    f32 *t = &l.light_data[((usize)r * light_texels + 4) * 4];
    t[0] = p.x, t[1] = p.y, t[2] = p.z, t[3] = p.w;
  }
  i32 light_rows = l.light_rows;
  upload(l.light_tex, light_rows, light_texels, std::max(n, std::max(s.lamps.rows, 1)),
         RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, l.light_data.data());
  l.light_rows = light_rows;
  // The lamps' face matrices, columns as GLSL's mat4 takes them.
  l.mat_data.assign((usize)light3d_shadow_limit * 6 * 16, 0.0f);
  for (i32 f = 0; f < s.lamps.rows * 6; f++) {
    const float16 m = MatrixToFloatV(s.lamps.vp[(usize)f]);
    std::copy(m.v, m.v + 16, &l.mat_data[(usize)f * 16]);
  }
  if (l.lamp_mat_tex == 0) {
    l.lamp_mat_tex = rlLoadTexture(nullptr, light3d_shadow_limit * 6 * 4, 1, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
    rlTextureParameters(l.lamp_mat_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(l.lamp_mat_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
  }
  rlUpdateTexture(l.lamp_mat_tex, 0, 0, light3d_shadow_limit * 6 * 4, 1, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32,
                  l.mat_data.data());

  light_clusters_build(l,s.lights,s.camera,size);
  const i32 tx=l.tiles_x,ty=l.tiles_y,sz=l.slices;
  const i32 index_rows_needed=std::max(1,i32(l.index_data.size())/index_width);
  // clusterTex: a row per depth slice, tiles across it (row y of the tiles,
  // then x), so the shader's (tile.y * tiles x + tile.x, slice) lands here.
  i32 cluster_rows = l.cluster_tex != 0 ? sz : 0;
  if (l.cluster_tex == 0) {
    l.cluster_tex = rlLoadTexture(nullptr, tx * ty, sz, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
    rlTextureParameters(l.cluster_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(l.cluster_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
  }
  (void)cluster_rows;
  rlUpdateTexture(l.cluster_tex, 0, 0, tx * ty, sz, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, l.cluster_data.data());
  i32 index_rows = l.index_rows;
  upload(l.index_tex, index_rows, index_width, index_rows_needed, RL_PIXELFORMAT_UNCOMPRESSED_R32, l.index_data.data());
  l.index_rows = index_rows;
}

void lights_ext_bind(const lights_ext &l) {
  const struct {
    i32 unit;
    u32 tex;
  } maps[] = {{light_tex_unit, l.light_tex}, {cluster_tex_unit, l.cluster_tex}, {index_tex_unit, l.index_tex},
              {lamp_mat_unit, l.lamp_mat_tex}};
  for (const auto &m : maps) {
    rlActiveTextureSlot(m.unit);
    rlEnableTexture(m.tex);
  }
  rlActiveTextureSlot(0);
}

void lights_ext_uniforms(const render3d_state &s, Shader sh, f32 tile_scale) {
  const lights_ext &l = s.lext;
  set_i(sh, "lightTotal", l.total);
  set_i(sh, "lightTex", light_tex_unit);
  set_i(sh, "clusterTex", cluster_tex_unit);
  set_i(sh, "indexTex", index_tex_unit);
  set_i(sh, "lampMatTex", lamp_mat_unit);
  set_v4(sh, "clusterGrid", {(f32)l.tiles_x, (f32)l.tiles_y, (f32)l.slices, l.tile_px * tile_scale});
  set_v4(sh, "clusterDepth", l.depth);
}
} // namespace njin
