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

  // The clusters, in the camera's view space (x right, y up, z forward).
  const camera3d &cam = s.camera;
  const bool ortho = cam.projection == camera3d_orthographic;
  const vec3 fwd = normalize(cam.target - cam.position);
  const vec3 right = normalize(cross(fwd, cam.up));
  const vec3 up = cross(right, fwd);
  const f32 aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
  const f32 tan_y = std::tan(cam.fovy * 0.5f * DEG2RAD);
  const f32 half_y = ortho ? std::max(cam.ortho_height, 1e-4f) * 0.5f : 0.0f;
  const f32 near_d = std::max(cam.near_plane, 0.01f);
  // Slices only as deep as the farthest light reaches.
  f32 far_d = near_d * 2.0f;
  for (i32 i = 0; i < n; i++) {
    const light3d_source &src = s.lights[(usize)i];
    far_d = std::max(far_d, dot(src.position - cam.position, fwd) + src.radius);
  }
  far_d = std::min(far_d, std::max(cam.far_plane, near_d * 2.0f));
  const i32 tx = l.tiles_x, ty = l.tiles_y, sz = l.slices;
  l.tile_px = std::max(std::ceil(size.x / (f32)tx), std::ceil(size.y / (f32)ty));
  // The tiles cover tile_px * tiles pixels, a little past the screen's edge.
  const f32 span_x = l.tile_px * (f32)tx / std::max(size.x, 1.0f);
  const f32 span_y = l.tile_px * (f32)ty / std::max(size.y, 1.0f);
  const f32 log_ratio = std::log(far_d / near_d);
  l.depth = {near_d, far_d, log_ratio, ortho ? 1.0f : 0.0f};
  const auto slice_of = [&](f32 z) {
    const f32 t = ortho ? (z - near_d) / std::max(far_d - near_d, 1e-4f) : std::log(std::max(z, near_d) / near_d) / log_ratio;
    return std::clamp((i32)std::floor(t * (f32)sz), 0, sz - 1);
  };
  const auto slice_near = [&](i32 k) {
    const f32 t = (f32)k / (f32)sz;
    return ortho ? near_d + (far_d - near_d) * t : near_d * std::exp(log_ratio * t);
  };
  // The tile range a box [x0, x1] x [y0, y1] at depths [z0, z1] covers.
  const auto tiles_of = [&](f32 x0, f32 x1, f32 y0, f32 y1, f32 z0, f32 z1, i32 &a0, i32 &a1, i32 &b0, i32 &b1) {
    f32 u0, u1, v0, v1;
    if (ortho) {
      u0 = x0 / (half_y * aspect), u1 = x1 / (half_y * aspect);
      v0 = y0 / half_y, v1 = y1 / half_y;
    } else {
      const f32 zx = tan_y * aspect, zy = tan_y;
      u0 = std::min(x0 / (z0 * zx), x0 / (z1 * zx)), u1 = std::max(x1 / (z0 * zx), x1 / (z1 * zx));
      v0 = std::min(y0 / (z0 * zy), y0 / (z1 * zy)), v1 = std::max(y1 / (z0 * zy), y1 / (z1 * zy));
    }
    // NDC to tiles: the tiles start at the bottom left, as gl_FragCoord does.
    const auto tile = [](f32 ndc, f32 span, i32 count) { return (ndc * 0.5f + 0.5f) / span * (f32)count; };
    a0 = std::clamp((i32)std::floor(tile(u0, span_x, tx)), 0, tx - 1);
    a1 = std::clamp((i32)std::floor(tile(u1, span_x, tx)), 0, tx - 1);
    b0 = std::clamp((i32)std::floor(tile(v0, span_y, ty)), 0, ty - 1);
    b1 = std::clamp((i32)std::floor(tile(v1, span_y, ty)), 0, ty - 1);
    return u1 >= -1.0f && u0 <= 1.0f && v1 >= -1.0f && v0 <= 1.0f;
  };
  const i32 clusters = tx * ty * sz;
  l.counts.assign((usize)clusters, 0);
  // Two passes over the lights: count, then fill.
  const auto each = [&](auto &&fn) {
    for (i32 i = 0; i < n; i++) {
      const light3d_source &src = s.lights[(usize)i];
      const vec3 rel = src.position - cam.position;
      const f32 x = dot(rel, right), y = dot(rel, up), z = dot(rel, fwd);
      const f32 r = std::max(src.radius, 0.001f);
      if (z + r < near_d || z - r > far_d)
        continue;
      const i32 s0 = slice_of(std::max(z - r, near_d)), s1 = slice_of(std::min(z + r, far_d));
      for (i32 k = s0; k <= s1; k++) {
        // The slice's own depth range keeps the tile box tight.
        const f32 z0 = std::max({z - r, slice_near(k), near_d}), z1 = std::min(z + r, slice_near(k + 1));
        if (z1 < z0)
          continue;
        i32 a0, a1, b0, b1;
        if (!tiles_of(x - r, x + r, y - r, y + r, std::max(z0, near_d), std::max(z1, near_d), a0, a1, b0, b1))
          continue;
        for (i32 b = b0; b <= b1; b++)
          for (i32 a = a0; a <= a1; a++)
            fn((k * ty + b) * tx + a, i);
      }
    }
  };
  each([&](i32 c, i32) { l.counts[(usize)c]++; });
  l.offsets.assign((usize)clusters, 0);
  i32 total = 0;
  for (i32 c = 0; c < clusters; c++) {
    l.offsets[(usize)c] = total;
    total += l.counts[(usize)c];
  }
  l.cluster_data.assign((usize)clusters * 4, 0.0f);
  for (i32 c = 0; c < clusters; c++) {
    l.cluster_data[(usize)c * 4] = (f32)l.offsets[(usize)c];
    l.cluster_data[(usize)c * 4 + 1] = (f32)l.counts[(usize)c];
  }
  const i32 index_rows_needed = std::max((total + index_width - 1) / index_width, 1);
  l.index_data.assign((usize)index_rows_needed * index_width, 0.0f);
  std::fill(l.counts.begin(), l.counts.end(), 0);
  each([&](i32 c, i32 i) { l.index_data[(usize)(l.offsets[(usize)c] + l.counts[(usize)c]++)] = (f32)i; });
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
