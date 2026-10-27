#include "render3d_cull.h"
#include "render3d.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_instance.h"
#include "njin_log.h"
#include "render_stats.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <raymath.h>
#include <rlgl.h>

// raylib does not wrap pixel buffers, fences or reading a buffer back, so
// those few GL calls are loaded here, as njin_gpu_caps.cpp loads glGetString.
extern "C" void (*glfwGetProcAddress(const char *name))(void);

#if defined(_WIN32)
#define NJIN_GLAPI __stdcall
#else
#define NJIN_GLAPI
#endif

namespace njin {
namespace {
constexpr u32 gl_array_buffer = 0x8892;
constexpr u32 gl_pixel_pack_buffer = 0x88EB;
constexpr u32 gl_stream_read = 0x88E1;
constexpr u32 gl_red = 0x1903;
constexpr u32 gl_float = 0x1406;
constexpr u32 gl_map_read_bit = 0x0001;
constexpr u32 gl_pack_alignment = 0x0D05;
constexpr u32 gl_sync_gpu_commands_complete = 0x9117;
constexpr u32 gl_already_signaled = 0x911A;
constexpr u32 gl_condition_satisfied = 0x911C;
constexpr u32 gl_texture_2d = 0x0DE1;
constexpr u32 gl_viewport = 0x0BA2;
constexpr u32 gl_no_error = 0;

struct gl_fns {
  void(NJIN_GLAPI *gen_buffers)(i32, u32 *) = nullptr;
  void(NJIN_GLAPI *delete_buffers)(i32, const u32 *) = nullptr;
  void(NJIN_GLAPI *bind_buffer)(u32, u32) = nullptr;
  void(NJIN_GLAPI *buffer_data)(u32, std::ptrdiff_t, const void *, u32) = nullptr;
  void(NJIN_GLAPI *get_buffer_sub_data)(u32, std::ptrdiff_t, std::ptrdiff_t, void *) = nullptr;
  void(NJIN_GLAPI *read_pixels)(i32, i32, i32, i32, u32, u32, void *) = nullptr;
  void(NJIN_GLAPI *pixel_store)(u32, i32) = nullptr;
  void *(NJIN_GLAPI *map_buffer_range)(u32, std::ptrdiff_t, std::ptrdiff_t, u32) = nullptr;
  u8(NJIN_GLAPI *unmap_buffer)(u32) = nullptr;
  void *(NJIN_GLAPI *fence_sync)(u32, u32) = nullptr;
  u32(NJIN_GLAPI *client_wait_sync)(void *, u32, u64) = nullptr;
  void(NJIN_GLAPI *delete_sync)(void *) = nullptr;
  void(NJIN_GLAPI *copy_tex_sub_image)(u32, i32, i32, i32, i32, i32, i32, i32) = nullptr;
  void(NJIN_GLAPI *get_integer)(u32, i32 *) = nullptr;
  u32(NJIN_GLAPI *get_error)() = nullptr;
  bool loaded = false;
  bool readback = false; // pixel buffers and fences: Hi-Z
};

template <typename T> void load(T &fn, const char *name) { fn = reinterpret_cast<T>(glfwGetProcAddress(name)); }

const gl_fns &gl() {
  static gl_fns g;
  if (!g.loaded) {
    g.loaded = true;
    load(g.gen_buffers, "glGenBuffers");
    load(g.delete_buffers, "glDeleteBuffers");
    load(g.bind_buffer, "glBindBuffer");
    load(g.buffer_data, "glBufferData");
    load(g.get_buffer_sub_data, "glGetBufferSubData");
    load(g.read_pixels, "glReadPixels");
    load(g.pixel_store, "glPixelStorei");
    load(g.map_buffer_range, "glMapBufferRange");
    load(g.unmap_buffer, "glUnmapBuffer");
    load(g.fence_sync, "glFenceSync");
    load(g.client_wait_sync, "glClientWaitSync");
    load(g.delete_sync, "glDeleteSync");
    load(g.copy_tex_sub_image, "glCopyTexSubImage2D");
    load(g.get_integer, "glGetIntegerv");
    load(g.get_error, "glGetError");
    g.readback = g.gen_buffers && g.delete_buffers && g.bind_buffer && g.buffer_data && g.read_pixels &&
                 g.pixel_store && g.map_buffer_range && g.unmap_buffer && g.fence_sync && g.client_wait_sync &&
                 g.delete_sync && g.copy_tex_sub_image && g.get_integer && g.get_error;
  }
  return g;
}

// Each texel the farthest depth of the source pixels under it, so a box behind
// it is behind every surface there.
const char *const reduce_fs = R"(#version 330
uniform sampler2D texture0;
uniform ivec2 srcSize;
uniform ivec2 dstSize;
out vec4 finalColor;
void main() {
  ivec2 d = ivec2(gl_FragCoord.xy);
  ivec2 lo = d * srcSize / dstSize;
  ivec2 hi = min(((d + 1) * srcSize + dstSize - 1) / dstSize, srcSize);
  float m = 0.0;
  for (int y = lo.y; y < hi.y; y++)
    for (int x = lo.x; x < hi.x; x++)
      m = max(m, texelFetch(texture0, ivec2(x, y), 0).r);
  finalColor = vec4(m, 0.0, 0.0, 1.0);
}
)";

// Width of the reduced depth: a few pixels of the screen per texel is plenty
// for boxes, and the read-back stays small (320 x 180 floats is 225 KB).
constexpr i32 hiz_width = 320;

vec3 forward_of(const camera3d &cam) {
  const vec3 d = cam.target - cam.position;
  const f32 len = length(d);
  return len > 0.0f ? d * (1.0f / len) : vec3{0.0f, 0.0f, -1.0f};
}

void drop_fences(hiz_view &v) {
  for (void *&f : v.fence)
    if (f != nullptr) {
      gl().delete_sync(f);
      f = nullptr;
    }
}

// The read-back in ring slot `r` arrived: its depth becomes the view's mip chain.
void take(hiz_view &v, i32 r) {
  const i32 w = v.pw[(usize)r], h = v.ph[(usize)r];
  const gl_fns &g = gl();
  g.bind_buffer(gl_pixel_pack_buffer, v.pbo[(usize)r]);
  const void *data = g.map_buffer_range(gl_pixel_pack_buffer, 0, (std::ptrdiff_t)w * h * 4, gl_map_read_bit);
  if (data != nullptr) {
    v.mips.resize(1);
    v.mw.assign(1, w);
    v.mh.assign(1, h);
    v.mips[0].resize((usize)w * (usize)h);
    std::memcpy(v.mips[0].data(), data, (usize)w * (usize)h * sizeof(f32));
    g.unmap_buffer(gl_pixel_pack_buffer);
    // Each next level the farthest of the 2 x 2 under it (one texel at an odd edge).
    while (v.mw.back() > 1 || v.mh.back() > 1) {
      const i32 pw = v.mw.back(), ph = v.mh.back();
      const i32 nw = std::max(1, (pw + 1) / 2), nh = std::max(1, (ph + 1) / 2);
      std::vector<f32> next((usize)nw * (usize)nh);
      const std::vector<f32> &prev = v.mips.back();
      for (i32 y = 0; y < nh; y++)
        for (i32 x = 0; x < nw; x++) {
          const i32 x0 = x * 2, y0 = y * 2, x1 = std::min(x0 + 1, pw - 1), y1 = std::min(y0 + 1, ph - 1);
          next[(usize)(y * nw + x)] = std::max(std::max(prev[(usize)(y0 * pw + x0)], prev[(usize)(y0 * pw + x1)]),
                                               std::max(prev[(usize)(y1 * pw + x0)], prev[(usize)(y1 * pw + x1)]));
        }
      v.mips.push_back(std::move(next));
      v.mw.push_back(nw);
      v.mh.push_back(nh);
    }
    v.cpu_vp = v.vp[(usize)r];
    v.cpu_eye = v.eye[(usize)r];
    v.cpu_fwd = v.fwd[(usize)r];
    v.cpu_ortho = v.ortho[(usize)r];
    v.valid = true;
  }
  g.bind_buffer(gl_pixel_pack_buffer, 0);
}
} // namespace

void cull3d_free(cull3d_state &c) {
  if (IsShaderValid(c.reduce))
    UnloadShader(c.reduce);
  c.reduce = Shader{};
  if (c.depth != 0)
    rlUnloadTexture(c.depth);
  c.depth = 0;
  for (hiz_view &v : c.views) {
    if (v.fbo != 0)
      rlUnloadFramebuffer(v.fbo);
    if (v.tex != 0)
      rlUnloadTexture(v.tex);
    if (gl().readback) {
      drop_fences(v);
      for (u32 &p : v.pbo)
        if (p != 0)
          gl().delete_buffers(1, &p);
    }
    v = hiz_view{};
  }
  for (std::vector<instance_scratch> &set : c.scratch) {
    for (instance_scratch &b : set)
      if (b.vbo != 0)
        rlUnloadVertexBuffer(b.vbo);
    set.clear();
  }
  c.scratch_used = 0;
}

void hiz_begin(context &ctx, render3d_state &s) {
  (void)ctx;
  cull3d_state &c = s.cull;
  c.view = nullptr;
  c.testing = false;
  c.passes++;
  if (!s.options.occlusion || !gl().readback)
    return;
  const vec3 eye = s.camera.position;
  const vec3 fwd = forward_of(s.camera);
  const bool ortho = s.camera.projection == camera3d_orthographic;
  const f32 move = std::max(s.options.occlusion_max_move, 0.0f);
  // The view of this camera: the one whose last pass looked from about here.
  // Two cameras a frame (split screen) each keep their own.
  hiz_view *view = nullptr;
  f32 best = 0.0f;
  for (hiz_view &v : c.views) {
    if (!v.used || v.last_use == c.passes)
      continue;
    const f32 d = length(v.at_eye - eye);
    if (d <= std::max(move, 0.25f) && dot(v.at_fwd, fwd) >= 0.9f && v.at_ortho == ortho && (view == nullptr || d < best)) {
      view = &v;
      best = d;
    }
  }
  if (view == nullptr) {
    // A new camera, or one that jumped: the least recently used view, emptied.
    view = &c.views[0];
    for (hiz_view &v : c.views)
      if (v.last_use < view->last_use)
        view = &v;
    drop_fences(*view);
    view->valid = false;
    view->used = true;
  }
  view->last_use = c.passes;
  view->at_eye = eye;
  view->at_fwd = fwd;
  view->at_ortho = ortho;
  // Take in the read-backs that arrived, oldest first, so the newest wins.
  for (u32 i = 0; i < (u32)hiz_view::ring; i++) {
    const usize r = (view->next + i) % hiz_view::ring;
    void *&f = view->fence[r];
    if (f == nullptr)
      continue;
    const u32 st = gl().client_wait_sync(f, 0, 0);
    if (st != gl_already_signaled && st != gl_condition_satisfied)
      continue;
    gl().delete_sync(f);
    f = nullptr;
    take(*view, (i32)r);
  }
  c.view = view;
  c.testing = view->valid && length(view->cpu_eye - eye) <= move && dot(view->cpu_fwd, fwd) >= 0.95f &&
              view->cpu_ortho == ortho;
}

void hiz_capture(context &ctx, render3d_state &s, const Matrix &view_proj) {
  (void)ctx;
  cull3d_state &c = s.cull;
  hiz_view *v = c.view;
  if (v == nullptr)
    return;
  if (!c.tried) {
    c.tried = true;
    c.reduce = LoadShaderFromMemory(nullptr, reduce_fs);
    c.ok = IsShaderValid(c.reduce);
    if (!c.ok)
      NJIN_WARN("3d: the depth reduction shader failed to compile; render3d_options::occlusion is off");
  }
  if (!c.ok)
    return;
  const usize r = v->next;
  if (v->fence[r] != nullptr)
    return; // the GPU has not finished the read-back three passes ago: skip this one
  const gl_fns &g = gl();
  // The pass's depth, copied out of whatever it draws into (the window's
  // depth is no texture), over the pass's viewport.
  rlDrawRenderBatchActive();
  i32 vp[4] = {};
  g.get_integer(gl_viewport, vp);
  const i32 w = vp[2], h = vp[3];
  if (w <= 0 || h <= 0)
    return;
  if (c.depth == 0 || c.depth_w != w || c.depth_h != h) {
    if (c.depth != 0)
      rlUnloadTexture(c.depth);
    c.depth = rlLoadTextureDepth(w, h, false);
    c.depth_w = w;
    c.depth_h = h;
  }
  while (g.get_error() != gl_no_error) {
  }
  rlActiveTextureSlot(0);
  rlEnableTexture(c.depth);
  g.copy_tex_sub_image(gl_texture_2d, 0, 0, 0, vp[0], vp[1], w, h);
  rlDisableTexture();
  if (g.get_error() != gl_no_error) {
    // A multisampled window, say: its depth cannot be copied.
    NJIN_WARN("3d: the pass's depth cannot be copied; render3d_options::occlusion is off");
    c.ok = false;
    return;
  }
  const u32 depth_tex = c.depth;
  const i32 dw = std::min(w, hiz_width);
  const i32 dh = std::max(1, (i32)std::lround((f64)h * dw / w));
  if (v->fbo == 0 || v->w != dw || v->h != dh) {
    if (v->fbo != 0)
      rlUnloadFramebuffer(v->fbo);
    if (v->tex != 0)
      rlUnloadTexture(v->tex);
    v->tex = rlLoadTexture(nullptr, dw, dh, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    v->fbo = rlLoadFramebuffer();
    rlFramebufferAttach(v->fbo, v->tex, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    v->w = dw;
    v->h = dh;
    if (!rlFramebufferComplete(v->fbo)) {
      NJIN_WARN("3d: the occlusion depth target is incomplete; render3d_options::occlusion is off");
      c.ok = false;
      return;
    }
  }
  if (v->pbo[r] == 0)
    g.gen_buffers(1, &v->pbo[r]);
  if (v->pw[r] != dw || v->ph[r] != dh) {
    g.bind_buffer(gl_pixel_pack_buffer, v->pbo[r]);
    g.buffer_data(gl_pixel_pack_buffer, (std::ptrdiff_t)dw * dh * 4, nullptr, gl_stream_read);
    g.bind_buffer(gl_pixel_pack_buffer, 0);
    v->pw[r] = dw;
    v->ph[r] = dh;
  }

  rlDrawRenderBatchActive();
  rlEnableFramebuffer(v->fbo);
  rlViewport(0, 0, dw, dh);
  rlSetFramebufferWidth(dw);
  rlSetFramebufferHeight(dh);
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlOrtho(0, dw, dh, 0, 0.0, 1.0);
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlDisableDepthTest();
  BeginShaderMode(c.reduce);
  const i32 src[2] = {w, h}, dst[2] = {dw, dh};
  SetShaderValue(c.reduce, GetShaderLocation(c.reduce, "srcSize"), src, SHADER_UNIFORM_IVEC2);
  SetShaderValue(c.reduce, GetShaderLocation(c.reduce, "dstSize"), dst, SHADER_UNIFORM_IVEC2);
  const Texture2D depth{depth_tex, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R32};
  DrawTexturePro(depth, Rectangle{0.0f, 0.0f, (f32)w, (f32)h}, Rectangle{0.0f, 0.0f, (f32)dw, (f32)dh},
                 Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  EndShaderMode();
  rlDrawRenderBatchActive();

  g.pixel_store(gl_pack_alignment, 4);
  g.bind_buffer(gl_pixel_pack_buffer, v->pbo[r]);
  g.read_pixels(0, 0, dw, dh, gl_red, gl_float, nullptr);
  g.bind_buffer(gl_pixel_pack_buffer, 0);
  v->fence[r] = g.fence_sync(gl_sync_gpu_commands_complete, 0);
  v->vp[r] = view_proj;
  v->eye[r] = s.camera.position;
  v->fwd[r] = forward_of(s.camera);
  v->ortho[r] = s.camera.projection == camera3d_orthographic;
  v->next = (u32)((r + 1) % hiz_view::ring);
  rlDisableFramebuffer();
}

bool hiz_hidden(const cull3d_state &c, vec3 center, vec3 extent) {
  if (!c.testing || c.view == nullptr || c.view->mips.empty())
    return false;
  const hiz_view &v = *c.view;
  const Matrix &m = v.cpu_vp;
  f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f, z0 = 1e30f;
  for (i32 i = 0; i < 8; i++) {
    const vec3 p{center.x + ((i & 1) ? extent.x : -extent.x), center.y + ((i & 2) ? extent.y : -extent.y),
                 center.z + ((i & 4) ? extent.z : -extent.z)};
    const f32 cw = m.m3 * p.x + m.m7 * p.y + m.m11 * p.z + m.m15;
    // A box reaching behind the eye or through the near plane is not tested.
    if (cw <= 1e-5f)
      return false;
    const f32 cx = (m.m0 * p.x + m.m4 * p.y + m.m8 * p.z + m.m12) / cw;
    const f32 cy = (m.m1 * p.x + m.m5 * p.y + m.m9 * p.z + m.m13) / cw;
    const f32 cz = (m.m2 * p.x + m.m6 * p.y + m.m10 * p.z + m.m14) / cw;
    if (cz < -1.0f)
      return false;
    x0 = std::min(x0, cx), x1 = std::max(x1, cx);
    y0 = std::min(y0, cy), y1 = std::max(y1, cy);
    z0 = std::min(z0, cz);
  }
  // Wholly off the image the depth was drawn for: the frustum test's business.
  if (x1 < -1.0f || x0 > 1.0f || y1 < -1.0f || y0 > 1.0f)
    return false;
  const i32 w = v.mw[0], h = v.mh[0];
  const f32 u0 = std::clamp((x0 * 0.5f + 0.5f) * (f32)w, 0.0f, (f32)w - 1.0f);
  const f32 u1 = std::clamp((x1 * 0.5f + 0.5f) * (f32)w, 0.0f, (f32)w - 1.0f);
  const f32 v0 = std::clamp((y0 * 0.5f + 0.5f) * (f32)h, 0.0f, (f32)h - 1.0f);
  const f32 v1 = std::clamp((y1 * 0.5f + 0.5f) * (f32)h, 0.0f, (f32)h - 1.0f);
  // The level where the box covers at most 2 x 2 texels.
  const f32 span = std::max({u1 - u0, v1 - v0, 1.0f});
  i32 level = std::clamp((i32)std::ceil(std::log2(span)), 0, (i32)v.mips.size() - 1);
  const i32 lw = v.mw[(usize)level], lh = v.mh[(usize)level];
  const i32 tx0 = std::min((i32)u0 >> level, lw - 1), tx1 = std::min((i32)u1 >> level, lw - 1);
  const i32 ty0 = std::min((i32)v0 >> level, lh - 1), ty1 = std::min((i32)v1 >> level, lh - 1);
  const std::vector<f32> &mip = v.mips[(usize)level];
  f32 farthest = 0.0f;
  for (i32 y = ty0; y <= ty1; y++)
    for (i32 x = tx0; x <= tx1; x++)
      farthest = std::max(farthest, mip[(usize)(y * lw + x)]);
  const f32 nearest = z0 * 0.5f + 0.5f;
  return nearest > farthest;
}

void hiz_mark(render3d_state &s) {
  if (!s.cull.testing)
    return;
  for (draw3d_cmd &c : s.cmds)
    if (c.boxed && !c.culled && c.world == world3d_none && c.buffer.id == 0)
      c.occluded = hiz_hidden(s.cull, c.box_center, c.box_extent);
}

void instances_select(context &ctx, render3d_state &s, const draw3d_cmd &c, f32 radius, u32 lods, f32 lod_screen,
                      const std::array<vec4, 6> *planes, bool camera, std::vector<instance_group> &out) {
  out.clear();
  instance_slot *slot = instance_slot_of(ctx_ref<instance_store>(ctx), c.buffer);
  if (slot == nullptr || slot->vbo == 0 || c.first >= slot->count)
    return;
  const u32 first = c.first;
  const u32 count = std::min(c.count, slot->count - first);
  if (count == 0)
    return;
  const u32 floats = slot->floats;
  const usize need = (usize)slot->count * floats;
  // Keep a copy of every next upload; the first time, read the buffer back.
  if (!slot->keep_cpu || slot->cpu.size() < need) {
    slot->keep_cpu = true;
    const gl_fns &g = gl();
    if (slot->cpu.size() < need && g.get_buffer_sub_data != nullptr && g.bind_buffer != nullptr) {
      slot->cpu.resize(need);
      g.bind_buffer(gl_array_buffer, slot->vbo);
      g.get_buffer_sub_data(gl_array_buffer, 0, (std::ptrdiff_t)(need * sizeof(f32)), slot->cpu.data());
      g.bind_buffer(gl_array_buffer, 0);
    }
  }
  if (slot->cpu.size() < need) {
    out.push_back({slot->vbo, first, count, 0});
    return;
  }
  const bool test_hidden = camera && s.cull.testing;
  const bool pick_lod = c.inst_lod && lods > 0;
  const vec3 eye = s.camera.position;
  const bool ortho = s.camera.projection == camera3d_orthographic;
  const f32 view_half = ortho ? std::max(s.camera.ortho_height * 0.5f, 1e-4f) : s.tan_half_fovy;
  // Kept between calls: no allocation per draw once they have grown.
  thread_local std::array<std::vector<f32>, 5> kept;
  const u32 levels = std::min<u32>(lods, (u32)kept.size() - 1);
  for (u32 l = 0; l <= levels; l++)
    kept[l].clear();
  u32 culled = 0;
  // The planes with unit normals, so a sphere's radius compares with their offset.
  std::array<vec4, 6> unit{};
  if (planes != nullptr)
    for (usize k = 0; k < unit.size(); k++) {
      const vec4 &pl = (*planes)[k];
      const f32 len = std::sqrt(pl.x * pl.x + pl.y * pl.y + pl.z * pl.z);
      const f32 inv = len > 0.0f ? 1.0f / len : 0.0f;
      unit[k] = {pl.x * inv, pl.y * inv, pl.z * inv, pl.w * inv};
    }
  const f32 *data = slot->cpu.data();
  for (u32 i = first; i < first + count; i++) {
    const f32 *in = data + (usize)i * floats;
    const vec3 p{in[0], in[1], in[2]};
    f32 k = in[3] != 0.0f ? std::abs(in[3]) : 1.0f;
    if (floats >= 16) {
      f32 axis = 0.0f;
      for (i32 a = 12; a < 15; a++)
        axis = std::max(axis, in[a] != 0.0f ? std::abs(in[a]) : 1.0f);
      k *= axis;
    }
    const f32 r = radius * k;
    bool out_of_view = false;
    if (planes != nullptr)
      for (const vec4 &pl : unit)
        if (pl.x * p.x + pl.y * p.y + pl.z * p.z + pl.w < -r) {
          out_of_view = true;
          break;
        }
    if (!out_of_view && test_hidden)
      out_of_view = hiz_hidden(s.cull, p, vec3{r, r, r});
    if (out_of_view) {
      culled++;
      continue;
    }
    u32 lod = 0;
    if (pick_lod) {
      const f32 dist = length(p - eye);
      if (dist > r) {
        const f32 share = ortho ? r / view_half : r / (dist * view_half);
        f32 below = lod_screen;
        while (lod < levels && share < below) {
          lod++;
          below *= 0.5f;
        }
      }
    }
    kept[lod].insert(kept[lod].end(), in, in + floats);
  }
  if (camera) {
    render_stats &stats = ctx_ref<render_stats>(ctx);
    stats.instances3d += count - culled;
    stats.instances3d_culled += culled;
  }
  // All kept at the full level: the draw's own buffer, as it is.
  if (culled == 0 && kept[0].size() == (usize)count * floats) {
    out.push_back({slot->vbo, first, count, 0});
    return;
  }
  for (u32 l = 0; l <= levels; l++)
    if (!kept[l].empty())
      out.push_back({scratch_upload(s.cull, kept[l].data(), (i32)(kept[l].size() * sizeof(f32))), 0,
                     (u32)(kept[l].size() / floats), (u8)l});
}

u32 scratch_upload(cull3d_state &c, const void *data, i32 bytes) {
  std::vector<instance_scratch> &set = c.scratch[c.scratch_set];
  if (c.scratch_used == set.size())
    set.push_back({});
  instance_scratch &b = set[c.scratch_used++];
  if (b.vbo == 0 || bytes > b.capacity) {
    if (b.vbo != 0)
      rlUnloadVertexBuffer(b.vbo);
    b.capacity = std::max({bytes, b.capacity + b.capacity / 2, 16 * 1024});
    b.vbo = rlLoadVertexBuffer(nullptr, b.capacity, true);
  }
  rlUpdateVertexBuffer(b.vbo, data, bytes, 0);
  return b.vbo;
}

void instances_pass_end(cull3d_state &c) {
  c.scratch_set = (c.scratch_set + 1) % cull3d_state::scratch_sets;
  c.scratch_used = 0;
}
} // namespace njin
