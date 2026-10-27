#pragma once
#include "../njin_internal_only.h"
#include "njin_3d.h"
#include <raylib.h>
#include <array>
#include <vector>

namespace njin {
struct context;
struct draw3d_cmd;
struct render3d_state;

// Occlusion culling (render3d_options::occlusion) and the per-instance culling
// and level of detail of draw_instanced3d with an instanced3d_desc.
//
// Hi-Z: after the opaque draws of a world pass, its depth is reduced on the GPU
// to a small image (each texel the farthest depth under it) and read back
// through a pixel buffer, fenced, so nothing waits for the GPU. A later pass
// with about the same camera builds a max-depth mip chain from it on the CPU
// and tests boxes against it, projected through the matrices the depth was
// drawn with: a box whose nearest point lies behind every depth it covers is
// hidden. The depth is one to a few frames old, so a camera that moved or
// turned too far since does not use it.

// One camera's reduced depth: the GPU target, the read-backs in flight, and
// the newest one that arrived as a mip chain.
struct hiz_view {
  static constexpr i32 ring = 3;
  u32 fbo = 0, tex = 0;
  i32 w = 0, h = 0;
  std::array<u32, ring> pbo{};
  std::array<void *, ring> fence{};
  std::array<Matrix, ring> vp{};
  std::array<vec3, ring> eye{}, fwd{};
  std::array<bool, ring> ortho{};
  std::array<i32, ring> pw{}, ph{};
  u32 next = 0;     // ring slot the next read-back goes to
  u64 last_use = 0; // pass count when a pass last took this view
  // The newest depth on the CPU: level 0 is pw x ph, each next level half.
  std::vector<std::vector<f32>> mips;
  std::vector<i32> mw, mh;
  Matrix cpu_vp{};
  vec3 cpu_eye{}, cpu_fwd{};
  bool cpu_ortho = false;
  bool valid = false;
  bool used = false; // a camera was matched to it
  // Where the camera of the last pass that took it looked from.
  vec3 at_eye{}, at_fwd{};
  bool at_ortho = false;
};

// A scratch instance buffer: the instances one draw keeps in one pass.
struct instance_scratch {
  u32 vbo = 0;
  i32 capacity = 0; // bytes
};

struct cull3d_state {
  Shader reduce{};
  bool tried = false, ok = false;
  u32 depth = 0; // the pass's depth, copied (the window's is no texture)
  i32 depth_w = 0, depth_h = 0;
  std::array<hiz_view, 4> views{};
  hiz_view *view = nullptr;    // the open pass's (nullptr: no occlusion this pass)
  bool testing = false;        // `view` has a depth this pass may test against
  u64 passes = 0;
  // Scratch buffers, one set per pass in turn: a set is written again only
  // four passes later, when the GPU has long finished reading it (writing a
  // buffer a frame in flight still reads makes some drivers wait for it).
  static constexpr usize scratch_sets = 4;
  std::array<std::vector<instance_scratch>, scratch_sets> scratch{};
  usize scratch_set = 0;
  usize scratch_used = 0;
};

// Frees the GL objects (render3d_state's destructor).
void cull3d_free(cull3d_state &c);

// Start of a world pass: picks the view of its camera, takes in the read-backs
// that arrived, and says whether boxes may be tested this pass (`testing`).
void hiz_begin(context &ctx, render3d_state &s);
// After the pass's opaque draws, with the pass's target and viewport bound:
// copies its depth, reduces it and starts its read-back, with `view_proj` the
// pass's matrices. Leaves the target and matrices changed: the caller binds
// the pass again.
void hiz_capture(context &ctx, render3d_state &s, const Matrix &view_proj);
// Whether the box (centre, half size) is hidden behind the depth being tested.
bool hiz_hidden(const cull3d_state &c, vec3 center, vec3 extent);
// Marks the open pass's boxed draws hidden behind the depth (draw3d_cmd::occluded).
void hiz_mark(render3d_state &s);

// A run of instances to draw with one level of detail: `count` from `first` in `vbo`.
struct instance_group {
  u32 vbo = 0;
  u32 first = 0;
  u32 count = 0;
  u8 lod = 0;
};

// The instances of a draw with an instanced3d_desc to draw in this pass: those
// inside `planes` (none: all) and, in the camera pass (`camera`), not hidden,
// grouped by level of detail (`lods` levels, the first below `lod_screen` of
// the screen). `radius` bounds an instance at scale 1 round its position.
// Kept instances are copied into scratch buffers; when every one is kept at
// the full level the draw's own buffer is used as it is.
void instances_select(context &ctx, render3d_state &s, const draw3d_cmd &c, f32 radius, u32 lods, f32 lod_screen,
                      const std::array<vec4, 6> *planes, bool camera, std::vector<instance_group> &out);
// `bytes` of `data` in a scratch buffer of the open pass (not written again
// before instances_pass_end()); returns the buffer.
u32 scratch_upload(cull3d_state &c, const void *data, i32 bytes);
// End of a pass: its scratch buffers wait their next turn.
void instances_pass_end(cull3d_state &c);
} // namespace njin
