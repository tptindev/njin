#pragma once
#include "../njin_internal_only.h"
#include "njin_post3d.h"
#include <raylib.h>
#include <array>
#include <vector>

namespace njin {
struct context;

// The 3D screen effects (njin_post3d.h) and decals, run by end_3d on a pass
// into the world target, which then has a depth texture (camera.cpp draws the
// world into it whenever post3d_wanted() says so).
//
//   post3d_after_opaque (after the opaque draws, before glass and water):
//     1. copy the target's colour and depth (one framebuffer blit),
//     2. decals: a box per decal, back faces, depth test off; each pixel finds
//        its world position from the copied depth and is kept when it falls
//        inside the box,
//     3. SSAO into a (half size) target, two depth-aware blur passes, then
//        multiplied into the image,
//     4. SSR: the reflecting surfaces drawn again into a mask (render3d), the
//        colour copied again, then each masked pixel marches its reflected ray
//        over the depth and takes the colour where it hits.
//   post3d_after_pass (after particles, before gizmos):
//     5. TAA (first world pass of the frame only, its projection jittered by
//        render3d): the colour copied, blended with the history reprojected
//        by depth, clipped to the colour round each pixel, history rejected
//        where the depth there does not match; the result sharpened back,
//     6. light shafts: the sky near the sun, blurred towards the sun at half
//        size, added,
//     7. lens flare: how much of the sun shows (1x1 pass over the depth),
//        then ghosts and a halo added,
//     8. motion blur: the colour copied again, smeared along the screen motion
//        of each pixel since the last pass's view-projection.
// end_3d binds the world target and the 3D camera again after each.
struct post3d_decal {
  decal3d_desc desc{};
  Matrix to_local{}; // world to the unit box (-0.5..0.5) of the decal
  f32 age = 0.0f;
  u32 gen = 0;
  u64 order = 0;     // when added, to replace the oldest
  bool alive = false;
};

struct post3d_state {
  post3d settings{};
  std::vector<post3d_decal> decals;
  i32 max_decals = 256;
  i32 live = 0;
  u64 next_order = 1;

  bool loaded = false;
  bool failed = false;
  Shader ssao{}, ssao_blur{}, ssao_apply{}, ssr{}, decal{}, shafts_sky{}, shafts_blur{}, add{}, flare_vis{}, flare{},
      blur{};
  // The copy of the target: colour and depth, the target's size.
  u32 copy_fbo = 0, copy_color = 0, copy_depth = 0;
  i32 w = 0, h = 0;
  RenderTexture2D ao_a{}, ao_b{};       // SSAO, full or half size
  RenderTexture2D mask{};               // SSR: how much each pixel reflects
  RenderTexture2D shafts_a{}, shafts_b{}; // half size
  RenderTexture2D visible{};            // 1x1: how much of the sun shows
  Matrix prev_view_proj{}; // the last pass's, unjittered, for the motion blur
  f32 prev_time = 0.0f;
  bool has_prev = false;

  // TAA: two history images (half float) swapped each frame, and the depth
  // of the last resolved pass (with a colour attachment so the framebuffer
  // is complete everywhere).
  Shader taa{}, taa_out{};
  u32 hist_fbo[2]{}, hist_tex[2]{};
  u32 hdepth_fbo = 0, hdepth_color = 0, hdepth_tex = 0;
  i32 hist_w = 0, hist_h = 0;
  i32 hist_cur = 0;          // which history holds the last result
  bool has_history = false;
  Matrix hist_view_proj{};   // the last resolved pass's, unjittered
  vec3 hist_eye{}, hist_dir{};
  f64 hist_time = 0.0;       // GetTime() then: a gap means a stale history
  u32 taa_index = 0;         // Halton sample
  bool taa_claimed = false;  // a world pass of this frame is resolved
  bool taa_pass = false;     // the open pass is that one

  post3d_state() = default;
  ~post3d_state();
  post3d_state(const post3d_state &) = delete;
  post3d_state &operator=(const post3d_state &) = delete;
};

// Whether the world must be drawn into the world target with a depth texture
// this frame: an effect is on or a decal exists.
bool post3d_wanted(const context &ctx);

// Called by end_3d on a pass into the world target (see above), with the
// pass's 3D camera loaded. They leave other targets and matrices bound.
void post3d_after_opaque(context &ctx);
void post3d_after_pass(context &ctx);

// Ages the decals by the game's frame time; phase_update.
void post3d_update(context &ctx);

// A new frame's world: no pass of it is resolved by TAA yet (camera.cpp).
void post3d_frame_begin(context &ctx);

// The jitter (NDC) for a world pass about to open: non-zero when TAA is on and
// this is the frame's first world pass, which post3d_after_pass then resolves.
vec2 post3d_taa_jitter(context &ctx);
} // namespace njin
