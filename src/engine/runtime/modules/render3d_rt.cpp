// Ray traced shadows (light3d::ray_traced_shadows); render3d_ext.h.
//
// The OpenGL side of the hybrid: end_3d draws the depth of the pass's opaque
// draws first (rt_prepass_begin, then render3d.cpp's draw_casters), which is
// copied here into an image shared with Vulkan; the pass's geometry goes to
// Vulkan as bottom-level structures (one per mesh, kept while it lives) and
// instances, and render3d_rt_vk.cpp traces the sun and up to three lamps into
// the shadow mask the lighting shaders then read per pixel. Without
// NJIN_RAYTRACING (CMake) the Vulkan side is not built and nothing traces.
//
// What goes into the scene: primitives, models (posed ones skinned on the CPU
// from the pass's bone matrices, at full detail), draw_instanced3d and the
// terrain. SDF shapes and grass receive the shadows but do not cast them.
#include "render3d.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_instance.h"
#include "njin_log.h"
#include "njin_model.h"
#include "njin_quality_impl.h"
#include "njin_texture.h"
#include "njin_world3d_impl.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <raymath.h>
#include <rlgl.h>
#include <vector>
#if NJIN_RAYTRACING
#include "render3d_rt_vk.h"
#endif

namespace njin {
// The Vulkan side's messages, through njin's log (0 info, 1 warning, 2 error).
void rt_log(int level, const char *text) {
  if (level >= 2)
    NJIN_ERROR("%s", text);
  else if (level == 1)
    NJIN_WARN("%s", text);
  else
    NJIN_INFO("%s", text);
}

#if NJIN_RAYTRACING
namespace {
const char *const copy_vs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
// The prepass's window depth, 0..1, into the shared R32F image.
const char *const copy_fs = R"(#version 330
uniform sampler2D texture0;
out vec4 finalColor;
void main() { finalColor = vec4(texelFetch(texture0, ivec2(gl_FragCoord.xy), 0).r); }
)";

bool transmits(const material3d &m) { return m.pbr && m.transmission > 0.0f; }

// A raylib matrix as the three rows of a 3 x 4 transform.
void rows_of(const Matrix &m, f32 out[12]) {
  const f32 r[12] = {m.m0, m.m4, m.m8, m.m12, m.m1, m.m5, m.m9, m.m13, m.m2, m.m6, m.m10, m.m14};
  std::memcpy(out, r, sizeof(r));
}

u64 key_mix(u64 a, u64 b) {
  u64 h = a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
  h ^= h >> 31;
  h *= 0xbf58476d1ce4e5b9ull;
  return h ^ (h >> 27);
}

// The bottom-level structure of a mesh at rest (its own vertex positions).
u64 mesh_blas(const Mesh &mesh) {
  rtvk::mesh_in in;
  in.pos = mesh.vertices;
  in.vertex_count = (u32)std::max(mesh.vertexCount, 0);
  in.idx16 = mesh.indices;
  in.index_count = mesh.indices != nullptr ? (u32)mesh.triangleCount * 3 : 0;
  // The vertex array id changes when a mesh is freed and another takes its address.
  return rtvk::mesh((u64)(uintptr_t)&mesh, mesh.vaoId * 2654435761u + (u32)mesh.vertexCount, in);
}

// The alpha of a texture (material3d::alpha_cutoff), read back once per texture.
struct alpha_image {
  i32 w = 0, h = 0;
  std::vector<u8> a;
};
const alpha_image &alpha_of(const Texture2D &t) {
  static std::unordered_map<u32, alpha_image> images;
  const auto [it, added] = images.try_emplace(t.id);
  if (added) {
    Image img = LoadImageFromTexture(t);
    if (img.data != nullptr && img.width > 0 && img.height > 0) {
      ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
      const u8 *px = (const u8 *)img.data;
      it->second.w = img.width, it->second.h = img.height;
      it->second.a.resize((usize)img.width * (usize)img.height);
      for (usize i = 0; i < it->second.a.size(); i++)
        it->second.a[i] = px[i * 4 + 3];
    }
    UnloadImage(img);
  }
  return it->second;
}

// A surface cut by its albedo's alpha (material3d::alpha_cutoff), as geometry:
// every triangle split into cut_steps x cut_steps small ones, those whose middle
// the alpha covers kept. Rays then pass between the leaves. Built once per mesh
// and image; a mesh without texture coordinates or an image is whole.
constexpr i32 cut_steps = 8;
u64 cut_blas(const Mesh &mesh, const Texture2D &albedo, f32 cutoff) {
  if (cutoff <= 0.0f || albedo.id == 0 || mesh.vertices == nullptr || mesh.texcoords == nullptr)
    return mesh_blas(mesh);
  struct entry {
    u32 version = 0;
    std::vector<f32> pos;
  };
  static std::unordered_map<u64, entry> cuts;
  const u64 key = key_mix((u64)(uintptr_t)&mesh, ((u64)albedo.id << 8) ^ (u64)std::lround(cutoff * 255.0f));
  const u32 version = mesh.vaoId * 2654435761u + (u32)mesh.vertexCount;
  entry &e = cuts[key];
  if (e.version != version || e.pos.empty()) {
    e.version = version;
    e.pos.clear();
    const alpha_image &img = alpha_of(albedo);
    const u8 limit = (u8)clamp(cutoff * 255.0f, 0.0f, 255.0f);
    const auto solid = [&](f32 u, f32 v) {
      if (img.a.empty())
        return true;
      const i32 x = (i32)((u - std::floor(u)) * (f32)img.w), y = (i32)((v - std::floor(v)) * (f32)img.h);
      return img.a[(usize)clamp(y, 0, img.h - 1) * (usize)img.w + (usize)clamp(x, 0, img.w - 1)] >= limit;
    };
    const i32 tris = mesh.indices != nullptr ? mesh.triangleCount : mesh.vertexCount / 3;
    for (i32 t = 0; t < tris; t++) {
      i32 v[3];
      for (i32 k = 0; k < 3; k++)
        v[k] = mesh.indices != nullptr ? mesh.indices[t * 3 + k] : t * 3 + k;
      const auto at = [&](f32 i, f32 j, i32 c) { // barycentric grid point, component c of xyz or uv
        const f32 a = i / cut_steps, b = j / cut_steps;
        if (c < 3)
          return mesh.vertices[v[0] * 3 + c] + (mesh.vertices[v[1] * 3 + c] - mesh.vertices[v[0] * 3 + c]) * a +
                 (mesh.vertices[v[2] * 3 + c] - mesh.vertices[v[0] * 3 + c]) * b;
        c -= 3;
        return mesh.texcoords[v[0] * 2 + c] + (mesh.texcoords[v[1] * 2 + c] - mesh.texcoords[v[0] * 2 + c]) * a +
               (mesh.texcoords[v[2] * 2 + c] - mesh.texcoords[v[0] * 2 + c]) * b;
      };
      const auto keep = [&](f32 i0, f32 j0, f32 i1, f32 j1, f32 i2, f32 j2) {
        const f32 ci = (i0 + i1 + i2) / 3.0f, cj = (j0 + j1 + j2) / 3.0f;
        if (!solid(at(ci, cj, 3), at(ci, cj, 4)))
          return;
        for (const auto &[i, j] : {std::pair{i0, j0}, std::pair{i1, j1}, std::pair{i2, j2}})
          for (i32 c = 0; c < 3; c++)
            e.pos.push_back(at(i, j, c));
      };
      for (i32 i = 0; i < cut_steps; i++)
        for (i32 j = 0; i + j < cut_steps; j++) {
          keep((f32)i, (f32)j, (f32)i + 1, (f32)j, (f32)i, (f32)j + 1);
          if (i + j + 1 < cut_steps)
            keep((f32)i + 1, (f32)j, (f32)i + 1, (f32)j + 1, (f32)i, (f32)j + 1);
        }
    }
  }
  rtvk::mesh_in in;
  in.pos = e.pos.data();
  in.vertex_count = (u32)(e.pos.size() / 3);
  return rtvk::mesh(key, version, in);
}

// The pass's scene, rebuilt each trace; its buffers kept between passes.
struct gather {
  std::vector<rtvk::instance_in> inst;
  std::vector<f32> skinned, terrain_pos, instances;
  std::vector<u32> terrain_idx;
};
gather &scratch() {
  static gather g;
  return g;
}

// How what a ray meets is lit: the surface's colour and glow, linear.
struct surface_in {
  vec3 albedo{0.8f, 0.8f, 0.8f};
  vec3 emission{};
  bool two_sided = false; // material drawn from both sides (model_material::double_sided)
};

f32 to_linear(f32 c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// The mean colour of a texture (sRGB, 0..1), worked out once per texture.
vec3 texture_mean(const Texture2D &t) {
  static std::unordered_map<u32, vec3> means;
  if (t.id == 0)
    return {1.0f, 1.0f, 1.0f};
  const auto it = means.find(t.id);
  if (it != means.end())
    return it->second;
  vec3 m{1.0f, 1.0f, 1.0f};
  Image img = LoadImageFromTexture(t);
  if (img.data != nullptr && img.width > 0 && img.height > 0) {
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const u8 *px = (const u8 *)img.data;
    const usize n = (usize)img.width * (usize)img.height;
    f64 r = 0.0, gg = 0.0, b = 0.0;
    for (usize i = 0; i < n; i++)
      r += px[i * 4], gg += px[i * 4 + 1], b += px[i * 4 + 2];
    m = {(f32)(r / (255.0 * (f64)n)), (f32)(gg / (255.0 * (f64)n)), (f32)(b / (255.0 * (f64)n))};
  }
  UnloadImage(img);
  means[t.id] = m;
  return m;
}

Texture2D texture_of(const context &ctx, texture_handle handle) {
  const texture_slot *slot = texture_slot_of(ctx_ref<texture_store>(ctx), handle);
  return slot != nullptr && !slot->packed ? slot->texture : Texture2D{};
}

// A surface of colour `c` (sRGB) times texture `t`, glowing `glow` (rgb, a the strength).
surface_in surface_of(rgba c, const Texture2D &t, rgba glow) {
  const vec3 m = texture_mean(t);
  surface_in out;
  out.albedo = {to_linear(c.r * m.x), to_linear(c.g * m.y), to_linear(c.b * m.z)};
  out.emission = vec3{to_linear(glow.r), to_linear(glow.g), to_linear(glow.b)} * std::max(glow.a, 0.0f);
  return out;
}

void add(gather &g, u64 blas, const Matrix &m, const surface_in &sf) {
  if (blas == 0)
    return;
  rtvk::instance_in in;
  in.blas = blas;
  rows_of(m, in.m);
  in.albedo[0] = sf.albedo.x, in.albedo[1] = sf.albedo.y, in.albedo[2] = sf.albedo.z;
  in.emission[0] = sf.emission.x, in.emission[1] = sf.emission.y, in.emission[2] = sf.emission.z;
  in.two_sided = sf.two_sided;
  g.inst.push_back(in);
}

// A model part's albedo image: the game's, else the file's.
Texture2D part_texture(const context &ctx, const model_slot &m, i32 mesh) {
  const i32 index = m.model.meshMaterial[mesh];
  Texture2D t = texture_of(ctx, m.materials[(usize)index].albedo);
  return t.id != 0 ? t : m.model.materials[index].maps[MATERIAL_MAP_DIFFUSE].texture;
}

// A model part's BLAS, cut by its material's alpha when it has a cutoff.
u64 part_blas(const context &ctx, const model_slot &m, i32 mesh, const Mesh &shape) {
  const model_material &mm = m.materials[(usize)m.model.meshMaterial[mesh]];
  return cut_blas(shape, part_texture(ctx, m, mesh), mm.surface.alpha_cutoff);
}

// A model part's surface: its material's colour and albedo image, the draw's
// tint, its glow.
surface_in part_surface(const context &ctx, const model_slot &m, i32 mesh, rgba tint) {
  const i32 index = m.model.meshMaterial[mesh];
  const model_material &mm = m.materials[(usize)index];
  const Texture2D t = part_texture(ctx, m, mesh);
  surface_in sf =
      surface_of({mm.color.r * tint.r, mm.color.g * tint.g, mm.color.b * tint.b, 1.0f}, t, mm.surface.emission);
  sf.two_sided = mm.double_sided;
  return sf;
}

// A posed mesh skinned on the CPU as the skinning shader does (the weights
// normalised), into its own structure for this pass.
u64 skinned_blas(const render3d_state &s, const draw3d_cmd &c, const Mesh &mesh, u64 key, gather &g) {
  if (mesh.boneIndices == nullptr || mesh.boneWeights == nullptr || mesh.vertices == nullptr)
    return mesh_blas(mesh);
  const i32 n = mesh.vertexCount;
  g.skinned.resize((usize)n * 3);
  for (i32 v = 0; v < n; v++) {
    const Vector3 p{mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]};
    Vector3 sum{0.0f, 0.0f, 0.0f};
    f32 wsum = 0.0f;
    for (i32 j = 0; j < 4; j++) {
      const f32 w = mesh.boneWeights[v * 4 + j];
      const u32 b = mesh.boneIndices[v * 4 + j];
      if (w <= 0.0f || b >= c.bone_count)
        continue;
      const Vector3 q = Vector3Transform(p, s.bones[c.bone_first + b]);
      sum = Vector3Add(sum, Vector3Scale(q, w));
      wsum += w;
    }
    const Vector3 out = wsum > 1e-4f ? Vector3Scale(sum, 1.0f / wsum) : p;
    g.skinned[(usize)v * 3] = out.x;
    g.skinned[(usize)v * 3 + 1] = out.y;
    g.skinned[(usize)v * 3 + 2] = out.z;
  }
  rtvk::mesh_in in;
  in.pos = g.skinned.data();
  in.vertex_count = (u32)n;
  in.idx16 = mesh.indices;
  in.index_count = mesh.indices != nullptr ? (u32)mesh.triangleCount * 3 : 0;
  return rtvk::mesh(key, s.rt.frame, in);
}

// The terrain as triangles, split along the same diagonal as its drawn mesh.
u64 terrain_blas(const terrain3d_slot &t, u32 id, gather &g) {
  const i32 r = t.res;
  if (r < 2 || (i32)t.heights.size() < r * r)
    return 0;
  const u64 key = key_mix(0x7465727261696eull, id);
  // Rebuilt only when it was edited; the arrays only then.
  rtvk::mesh_in in;
  g.terrain_pos.resize((usize)r * r * 3);
  for (i32 j = 0; j < r; j++)
    for (i32 i = 0; i < r; i++) {
      const usize k = (usize)(j * r + i);
      g.terrain_pos[k * 3] = t.desc.origin.x + (f32)i * t.spacing;
      g.terrain_pos[k * 3 + 1] = t.heights[k];
      g.terrain_pos[k * 3 + 2] = t.desc.origin.z + (f32)j * t.spacing;
    }
  g.terrain_idx.clear();
  g.terrain_idx.reserve((usize)(r - 1) * (r - 1) * 6);
  for (i32 j = 0; j + 1 < r; j++)
    for (i32 i = 0; i + 1 < r; i++) {
      const u32 a = (u32)(j * r + i), b = a + 1, c = a + (u32)r, d = c + 1;
      for (u32 v : {a, d, b, a, c, d})
        g.terrain_idx.push_back(v);
    }
  in.pos = g.terrain_pos.data();
  in.vertex_count = (u32)(r * r);
  in.idx32 = g.terrain_idx.data();
  in.index_count = (u32)g.terrain_idx.size();
  return rtvk::mesh(key, t.edits + 1, in);
}

// One draw_instanced3d instance's transform, as instanced_glsl places it:
// scale (instance0.w times instance3's axes), rotation z, x, y, then position.
Matrix instance_matrix(const f32 *d, u32 floats) {
  const f32 k = d[3] != 0.0f ? d[3] : 1.0f;
  vec3 axes{1.0f, 1.0f, 1.0f};
  if (floats >= 16)
    axes = {d[12] != 0.0f ? d[12] : 1.0f, d[13] != 0.0f ? d[13] : 1.0f, d[14] != 0.0f ? d[14] : 1.0f};
  Matrix m = MatrixScale(axes.x * k, axes.y * k, axes.z * k);
  if (floats >= 12) {
    m = MatrixMultiply(m, MatrixRotateZ(d[10] * DEG2RAD));
    m = MatrixMultiply(m, MatrixRotateX(d[8] * DEG2RAD));
    m = MatrixMultiply(m, MatrixRotateY(d[9] * DEG2RAD));
  }
  return MatrixMultiply(m, MatrixTranslate(d[0], d[1], d[2]));
}

void add_instanced(context &ctx, const draw3d_cmd &c, gather &g) {
  instance_store &store = ctx_ref<instance_store>(ctx);
  store.keep_cpu = true;
  instance_slot *slot = instance_slot_of(store, c.buffer);
  if (slot == nullptr || c.first >= slot->count || slot->floats < 4)
    return;
  const usize floats = slot->floats;
  if (slot->cpu.size() < (usize)slot->count * floats) {
    // Uploaded before ray tracing asked for copies: read it back once.
    slot->cpu.resize((usize)slot->count * floats);
    if (!rtvk::gl_read_buffer(slot->vbo, slot->cpu.size() * sizeof(f32), slot->cpu.data())) {
      slot->cpu.clear();
      return;
    }
  }
  std::vector<std::pair<u64, surface_in>> parts;
  if (c.mesh != nullptr) {
    if (c.material.cast_shadows && !c.material.unlit)
      parts.push_back({cut_blas(*c.mesh, texture_of(ctx, c.material.texture), c.material.alpha_cutoff),
                       surface_of(colors::white, texture_of(ctx, c.material.texture),
                                                       c.material.emission)});
  } else if (const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), c.model)) {
    for (i32 i = 0; i < m->model.meshCount; i++) {
      const model_material &mm = m->materials[(usize)m->model.meshMaterial[i]];
      if (mm.color.a >= 1.0f && !transmits(mm.surface) && mm.surface.cast_shadows)
        parts.push_back({part_blas(ctx, *m, i, m->model.meshes[i]), part_surface(ctx, *m, i, colors::white)});
    }
  }
  const u32 count = std::min(c.count, slot->count - c.first);
  for (u32 k = 0; k < count; k++) {
    const f32 *d = &slot->cpu[(usize)(c.first + k) * floats];
    const Matrix t = instance_matrix(d, slot->floats);
    // instance1: the instance's colour (sRGB), times each part's.
    for (const auto &[b, sf] : parts) {
      surface_in one = sf;
      if (slot->floats >= 8)
        one.albedo = {sf.albedo.x * to_linear(d[4]), sf.albedo.y * to_linear(d[5]), sf.albedo.z * to_linear(d[6])};
      add(g, b, t, one);
    }
  }
}

void add_model(context &ctx, const render3d_state &s, const draw3d_cmd &c, usize index, gather &g) {
  const model_slot *slot = model_slot_of(ctx_ref<model_store>(ctx), c.model);
  if (slot == nullptr)
    return;
  const Model &model = slot->model;
  const Matrix transform = MatrixMultiply(model.transform, c.transform);
  const bool posed = c.bone_count > 0;
  for (i32 i = 0; i < model.meshCount; i++) {
    const model_material &mm = slot->materials[(usize)model.meshMaterial[i]];
    if (mm.color.a < 1.0f || transmits(mm.surface) || !mm.surface.cast_shadows)
      continue;
    const surface_in sf = part_surface(ctx, *slot, i, c.color);
    if (posed) {
      add(g, skinned_blas(s, c, model.meshes[i], key_mix(index + 1, (u64)i), g), transform, sf);
      continue;
    }
    const model_lod_mesh *lod = model_lod_of(*slot, c.lod, i);
    add(g, part_blas(ctx, *slot, i, lod != nullptr ? lod->mesh : model.meshes[i]), transform, sf);
  }
}

void gather_scene(context &ctx, const render3d_state &s, gather &g) {
  g.inst.clear();
  for (usize i = 0; i < s.cmds.size(); i++) {
    const draw3d_cmd &c = s.cmds[i];
    if (c.color.a < 1.0f || c.fx.dissolve > 0.0f)
      continue;
    if (c.world != world3d_none) {
      if (c.world == world3d_terrain)
        if (const terrain3d_slot *t = terrain_of(ctx, terrain3d_handle{c.world_id})) {
          // Its first layer's colour stands for the ground.
          const terrain3d_layer &l = t->desc.layers[0];
          add(g, terrain_blas(*t, c.world_id, g), MatrixIdentity(),
              surface_of(l.color, texture_of(ctx, l.albedo), {}));
        }
      continue;
    }
    if (c.is_shape)
      continue;
    if (c.buffer.id != 0) {
      add_instanced(ctx, c, g);
      continue;
    }
    if (c.mesh != nullptr) {
      if (c.material.cast_shadows && !c.material.unlit && !transmits(c.material))
        add(g, cut_blas(*c.mesh, texture_of(ctx, c.material.texture), c.material.alpha_cutoff), c.transform,
            surface_of(c.color, texture_of(ctx, c.material.texture), c.material.emission));
      continue;
    }
    add_model(ctx, s, c, i, g);
  }
}

void free_prepass(rt_state &r) {
  if (r.pre_fbo != 0)
    rlUnloadFramebuffer(r.pre_fbo);
  if (r.copy_fbo != 0)
    rlUnloadFramebuffer(r.copy_fbo);
  if (r.pre_depth != 0)
    rlUnloadTexture(r.pre_depth);
  if (r.pre_color != 0)
    rlUnloadTexture(r.pre_color);
  r.pre_fbo = r.copy_fbo = r.pre_depth = r.pre_color = 0;
  r.depth_tex = r.mask_tex = 0; // owned by the Vulkan side
  r.w = r.h = 0;
}
} // namespace

bool rt_wanted(context &ctx, bool offscreen) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  rt_state &r = s.rt;
  r.wanted = false;
  r.live = false;
  r.reading = false;
  r.sun = false;
  r.reflect = false;
  r.gi = false;
  r.refl_live = false;
  r.lamp_count = 0;
  const bool grid = s.probes.grid_set && s.probes.grid.ray_traced;
  if (!(s.light.ray_traced_shadows || s.light.ray_traced_reflections || grid) || offscreen || s.cmds.empty())
    return false;
  if (!r.tried) {
    r.tried = true;
    r.ok = rtvk::init();
    if (r.ok)
      r.copy = LoadShaderFromMemory(copy_vs, copy_fs);
    r.ok = r.ok && IsShaderValid(r.copy);
  }
  if (!r.ok || !rtvk::ready())
    return false;
  const graphics_quality &q = ctx_ref<quality_state>(ctx)->q;
  r.sun = s.light.ray_traced_shadows && s.light.shadows && q.shadows;
  r.reflect = s.light.ray_traced_reflections;
  r.gi = grid;
  r.lamp.fill(-1);
  if (s.light.ray_traced_shadows && q.lamp_shadows) {
    const i32 count = std::min((i32)s.lights.size(), light3d_limit);
    for (i32 i = 0; i < count && r.lamp_count < rt_lamp_max; i++) {
      const light3d_source &l = s.lights[(usize)i];
      if (l.shadows && l.radius > 0.0f) {
        r.lamp[(usize)i] = (i8)r.lamp_count;
        r.lamp_light[(usize)r.lamp_count++] = i;
      }
    }
  }
  r.wanted = r.sun || r.lamp_count > 0 || r.reflect || r.gi;
  return r.wanted;
}

bool rt_prepass_begin(context &ctx, i32 w, i32 h) {
  rt_state &r = ctx_ref<render3d_state>(ctx)->rt;
  if (w <= 0 || h <= 0)
    return false;
  if (r.w != w || r.h != h || r.pre_fbo == 0) {
    free_prepass(r);
    unsigned depth = 0, mask = 0;
    if (!rtvk::targets(w, h, depth, mask)) {
      r.ok = false;
      return false;
    }
    r.depth_tex = depth;
    r.mask_tex = mask;
    r.pre_fbo = rlLoadFramebuffer();
    r.pre_color = rlLoadTexture(nullptr, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    r.pre_depth = rlLoadTextureDepth(w, h, false);
    rlFramebufferAttach(r.pre_fbo, r.pre_color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlFramebufferAttach(r.pre_fbo, r.pre_depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    r.copy_fbo = rlLoadFramebuffer();
    rlFramebufferAttach(r.copy_fbo, r.depth_tex, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    if (!rlFramebufferComplete(r.pre_fbo) || !rlFramebufferComplete(r.copy_fbo)) {
      NJIN_WARN("rt: the depth prepass targets are incomplete; ray tracing is off");
      free_prepass(r);
      r.ok = false;
      return false;
    }
    r.w = w;
    r.h = h;
    r.prev_ok = false; // new images: no history
  }
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(r.pre_fbo);
  rlViewport(0, 0, w, h);
  rlSetFramebufferWidth(w);
  rlSetFramebufferHeight(h);
  rlClearColor(255, 255, 255, 255);
  rlClearScreenBuffers();
  return true;
}

void rt_trace(context &ctx, const Matrix &view_proj) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  rt_state &r = s.rt;
  r.live = false;
  r.refl_live = false;
  const bool shadows = r.sun || r.lamp_count > 0;
  const bool depth = shadows || r.reflect;
  rlDrawRenderBatchActive();
  if (depth) {
    // The depth into the shared image.
    rlEnableFramebuffer(r.copy_fbo);
    rlViewport(0, 0, r.w, r.h);
    rlDisableDepthTest();
    rlDisableColorBlend();
    render3d_quad(r.copy, r.pre_depth);
    rlEnableColorBlend();
    rlDisableFramebuffer();
  }

  gather &g = scratch();
  gather_scene(ctx, s, g);
  rtvk::trace_in t;
  t.width = r.w;
  t.height = r.h;
  const float16 inv = MatrixToFloatV(MatrixInvert(view_proj));
  std::memcpy(t.inv_view_proj, inv.v, sizeof(t.inv_view_proj));
  const vec3 eye = render3d_post_eye(s.camera);
  t.eye[0] = eye.x, t.eye[1] = eye.y, t.eye[2] = eye.z;
  const vec3 to_sun = normalize(s.light.direction) * -1.0f;
  t.sun[0] = to_sun.x, t.sun[1] = to_sun.y, t.sun[2] = to_sun.z;
  t.sun[3] = std::tan(std::clamp(s.light.sun_angle, 0.0f, 30.0f) * 0.5f * DEG2RAD);
  t.shadows = shadows;
  t.sun_shadows = r.sun;
  t.rays = std::clamp(s.light.shadow_rays, 1, 16);
  t.blur = std::clamp(s.light.shadow_denoise, 0, 8);
  t.lamp_count = r.lamp_count;
  for (i32 k = 0; k < r.lamp_count; k++) {
    const light3d_source &l = s.lights[(usize)r.lamp_light[(usize)k]];
    t.lamps[k][0] = l.position.x, t.lamps[k][1] = l.position.y, t.lamps[k][2] = l.position.z;
    t.lamps[k][3] = std::max(l.source_size, 0.0f);
  }
  // How what the rays meet is lit, as the lighting shaders light it.
  const light3d &L = s.light;
  const rgb_linear sc = linear_of(L.color), ac = linear_of(L.ambient);
  t.sun_color[0] = sc.r, t.sun_color[1] = sc.g, t.sun_color[2] = sc.b;
  t.sun_color[3] = sc.r + sc.g + sc.b > 0.0f ? 1.0f : 0.0f;
  t.ambient[0] = ac.r, t.ambient[1] = ac.g, t.ambient[2] = ac.b;
  if (s.env.ready) {
    const f32 k = std::max(L.environment_intensity, 0.0f);
    t.ambient[3] = 1.0f;
    for (i32 i = 0; i < 9; i++)
      t.env_sh[i][0] = s.env.sh[i].x * k, t.env_sh[i][1] = s.env.sh[i].y * k, t.env_sh[i][2] = s.env.sh[i].z * k;
    t.env_sh[0][3] = L.environment_rotation * DEG2RAD;
  }
  t.light_count = std::min((i32)s.lights.size(), 8);
  for (i32 i = 0; i < t.light_count; i++) {
    const light3d_source &l = s.lights[(usize)i];
    const f32 half = std::clamp(l.cone, 0.0f, 179.0f) * 0.5f * DEG2RAD;
    const f32 inner = half * (1.0f - std::clamp(l.softness, 0.0f, 1.0f));
    const rgb_linear lc = linear_of(l.color);
    const vec3 d = normalize(l.direction);
    const f32 pos[4] = {l.position.x, l.position.y, l.position.z, std::max(l.radius, 0.001f)};
    const f32 col[4] = {lc.r * l.intensity, lc.g * l.intensity, lc.b * l.intensity, std::cos(inner)};
    const f32 spot[4] = {d.x, d.y, d.z, l.kind == light3d_spot ? std::cos(half) : -2.0f};
    std::memcpy(t.light_pos[i], pos, sizeof(pos));
    std::memcpy(t.light_color[i], col, sizeof(col));
    std::memcpy(t.light_spot[i], spot, sizeof(spot));
  }
  // The reflections' image, the size of the pass.
  if (r.reflect) {
    unsigned refl = 0;
    t.reflect = rtvk::reflect_target(r.w, r.h, refl);
    r.refl_tex = refl;
  }
  // The irradiance grid traced by rays.
  probe_state &p = s.probes;
  if (r.gi) {
    const irradiance3d_desc &gd = p.grid;
    const i32 nx = std::clamp(gd.count_x, 1, 16), ny = std::clamp(gd.count_y, 1, 16), nz = std::clamp(gd.count_z, 1, 16);
    unsigned tex = 0, depth_tex = 0;
    bool fresh = false;
    if (rtvk::grid_target(nx * ny * nz, tex, depth_tex, fresh)) {
      if (p.rt_reset && !fresh)
        rtvk::grid_reset();
      p.rt_reset = false;
      p.rt_tex = tex;
      p.rt_depth = depth_tex;
      const vec3 size{std::max(std::fabs(gd.size.x), 1e-3f), std::max(std::fabs(gd.size.y), 1e-3f),
                      std::max(std::fabs(gd.size.z), 1e-3f)};
      const vec3 lo = gd.center - size * 0.5f;
      const f32 mn[4] = {lo.x, lo.y, lo.z, std::clamp(gd.history, 0.0f, 0.99f)};
      const f32 sz[4] = {size.x, size.y, size.z, std::max(gd.intensity, 0.0f)};
      const f32 ct[4] = {(f32)nx, (f32)ny, (f32)nz, (f32)std::clamp(gd.rays, 16, 256)};
      std::memcpy(t.grid_min, mn, sizeof(mn));
      std::memcpy(t.grid_size, sz, sizeof(sz));
      std::memcpy(t.grid_count, ct, sizeof(ct));
      t.gi = true;
      t.grid_lights = true;
    }
  }
  // Last frame's view-projection: the shadows are blended over frames.
  t.history = r.prev_ok;
  const float16 pv = MatrixToFloatV(r.prev_vp), piv = MatrixToFloatV(MatrixInvert(r.prev_vp));
  std::memcpy(t.prev_view_proj, pv.v, sizeof(t.prev_view_proj));
  std::memcpy(t.prev_inv_view_proj, piv.v, sizeof(t.prev_inv_view_proj));
  // What moved since the last trace: round where it was and where it is,
  // as far as its size and two grid steps, the grid takes this trace's rays.
  if (t.gi) {
    const vec3 cnt{t.grid_count[0], t.grid_count[1], t.grid_count[2]};
    const vec3 gsize{t.grid_size[0], t.grid_size[1], t.grid_size[2]};
    const f32 step = std::max({gsize.x / std::max(cnt.x - 1.0f, 1.0f), gsize.y / std::max(cnt.y - 1.0f, 1.0f),
                               gsize.z / std::max(cnt.z - 1.0f, 1.0f)});
    const auto add_change = [&](vec3 at, f32 radius) {
      // One region per place: a thing dragged along refreshes its own.
      for (rt_state::change &c : r.changes)
        if (length(c.at - at) < 0.5f * (c.radius + radius)) {
          c.at = at;
          c.radius = std::max(c.radius, radius + 2.0f * step);
          c.until = r.frame + 8;
          return;
        }
      r.changes.push_back({at, radius + 2.0f * step, r.frame + 8});
    };
    for (const draw3d_cmd &c : s.cmds) {
      if (c.motion_key == 0 || c.world != world3d_none || c.buffer.id != 0)
        continue;
      const vec3 at = c.boxed ? c.box_center : vec3{c.transform.m12, c.transform.m13, c.transform.m14};
      const f32 radius = c.boxed ? length(c.box_extent) : 1.0f;
      auto [it, fresh] = r.moved_from.try_emplace(c.motion_key);
      rt_state::seen &was = it->second;
      if (!fresh && was.frame + 1 == r.frame) {
        const vec3 d = at - was.at;
        if (dot(d, d) > 1e-6f) {
          add_change(was.at, was.radius);
          add_change(at, radius);
        }
      }
      was = {at, radius, r.frame};
    }
    // Draws gone for a while are forgotten.
    if ((r.frame & 255u) == 0)
      std::erase_if(r.moved_from, [&](const auto &e) { return r.frame - e.second.frame > 60; });
    // The regions still fresh, the newest first, up to eight.
    std::erase_if(r.changes, [&](const rt_state::change &c) { return c.until < r.frame; });
    for (auto it = r.changes.rbegin(); it != r.changes.rend() && t.change_count < 8; ++it) {
      const f32 c[4] = {it->at.x, it->at.y, it->at.z, it->radius};
      std::memcpy(t.changes[t.change_count++], c, sizeof(c));
    }
  }
  t.frame = r.frame++;
  const bool ok = rtvk::trace(g.inst.data(), (u32)g.inst.size(), t);
  r.live = ok && shadows;
  r.prev_vp = view_proj;
  r.prev_ok = ok && shadows;
  r.refl_live = ok && t.reflect;
  if (t.gi)
    p.rt_live = p.rt_live || ok;
  // A mesh unused for a few seconds goes (its model was freed, or it left the scene).
  if ((r.frame & 63u) == 0)
    rtvk::collect(240);
}

void rt_bind(const rt_state &r) {
  if (r.live) {
    rlActiveTextureSlot(rt_mask_unit);
    rlEnableTexture(r.mask_tex);
  }
  if (r.refl_live) {
    rlActiveTextureSlot(rt_refl_unit);
    rlEnableTexture(r.refl_tex);
  }
  rlActiveTextureSlot(0);
}

void rt_free(rt_state &r) {
  free_prepass(r);
  if (IsShaderValid(r.copy))
    UnloadShader(r.copy);
  r.copy = Shader{};
  if (r.ok)
    rtvk::shutdown();
  r.ok = false;
}

bool raytracing_available(context &ctx) {
  rt_state &r = ctx_ref<render3d_state>(ctx)->rt;
  if (!r.tried) {
    r.tried = true;
    r.ok = rtvk::init();
    if (r.ok)
      r.copy = LoadShaderFromMemory(copy_vs, copy_fs);
    r.ok = r.ok && IsShaderValid(r.copy);
  }
  return r.ok && rtvk::ready();
}

#else // NJIN_RAYTRACING

bool rt_wanted(context &ctx, bool) {
  rt_state &r = ctx_ref<render3d_state>(ctx)->rt;
  r.wanted = r.live = r.reading = r.sun = r.reflect = r.gi = r.refl_live = false;
  r.lamp_count = 0;
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  const bool asked = s.light.ray_traced_shadows || s.light.ray_traced_reflections ||
                     (s.probes.grid_set && s.probes.grid.ray_traced);
  if (asked && !r.tried) {
    r.tried = true;
    NJIN_WARN("3d: ray tracing (light3d::ray_traced_*, irradiance3d_desc::ray_traced) needs njin built with "
              "NJIN_RAYTRACING; shadow maps, the environment and captured grids are used");
  }
  return false;
}
bool rt_prepass_begin(context &, i32, i32) { return false; }
void rt_trace(context &, const Matrix &) {}
void rt_bind(const rt_state &) {}
void rt_free(rt_state &) {}
bool raytracing_available(context &) { return false; }
#endif
} // namespace njin
