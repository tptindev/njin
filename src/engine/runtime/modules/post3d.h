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
//     5. light shafts: the sky near the sun, blurred towards the sun at half
//        size, added,
//     6. lens flare: how much of the sun shows (1x1 pass over the depth),
//        then ghosts and a halo added,
//     7. motion blur: the colour copied again, smeared along the screen motion
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
  Matrix prev_view_proj{}; // the last pass's, for the motion blur
  f32 prev_time = 0.0f;
  bool has_prev = false;

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
} // namespace njin
