#pragma once
#include "../njin_internal_only.h"
#include "_types.h"

namespace njin {
// What the world pass drew this frame, for the inspector. Reset at the start
// of each frame's rendering work (bake_tilemaps) and filled in as things draw.
//
// raylib does not say how many GL draw calls it made, so `batches` is an
// estimate: raylib starts a new one whenever the texture or the blend mode
// changes, and every instanced draw ends the current one. This is the number
// that grows when sprites of different textures are interleaved by layer or y.
struct render_stats {
  u32 sprites = 0;
  u32 sprites_culled = 0;
  u32 tile_chunks = 0;
  u32 emitters = 0;
  u32 emitters_culled = 0;
  u32 particles = 0;      // drawn, CPU and GPU
  u32 particles_gpu = 0;  // of which on the GPU
  u32 instanced_calls = 0;
  u32 batches = 0;        // estimated raylib draw calls, see above
  u32 post_passes = 0;    // full-screen passes of post_fx
  u32 lights = 0;         // lights drawn into the light map
  u32 models3d = 0;       // draw_model and model3d drawn by the camera pass
  u32 models3d_culled = 0; // left out, outside the camera's frustum
  u32 occluded3d = 0;      // left out, hidden behind others (render3d_options::occlusion)
  u32 instances3d = 0;     // drawn by draw_instanced3d with an instanced3d_desc
  u32 instances3d_culled = 0;
  u32 batches3d = 0;       // instanced draws render3d_options::batch made
  u32 batched3d = 0;       // the draws merged into them
  // CPU milliseconds end_3d spent in each stage this frame, summed over its
  // passes (a world pass and any offscreen ones): what the single thread does
  // to issue the frame, not what the GPU takes.
  f32 ms3d_setup = 0.0f;       // culling, occlusion views, environment, lights
  f32 ms3d_shadows = 0.0f;     // the sun's maps, the lamps', the cover map
  f32 ms3d_prepass = 0.0f;     // ray tracing: the depth prepass, the trace
  f32 ms3d_opaque = 0.0f;      // the sky and the opaque draws
  f32 ms3d_screen = 0.0f;      // occlusion depth, decals, SSAO, SSR, volumetric fog
  f32 ms3d_translucent = 0.0f; // glass, water, particles
  f32 ms3d_post = 0.0f;        // TAA/FXAA, HDR resolve, gizmos
  // The GPU's milliseconds in the same stages (render3d_gpu_timer.h), of the
  // passes whose timestamps came back this frame: a few frames behind.
  f32 gpu3d[7]{};

  // Texture id and blend mode of the batch being filled; 0 and -1 when none.
  u32 batch_texture = 0;
  i32 batch_blend = -1;

  // A draw with `texture` and `blend` is about to be issued.
  void note_draw(u32 texture, i32 blend) {
    if (batch_blend != blend || batch_texture != texture) {
      batches++;
      batch_texture = texture;
      batch_blend = blend;
    }
  }
  // Something flushed raylib's batch (an instanced draw, a shader switch).
  void note_flush() {
    batch_texture = 0;
    batch_blend = -1;
  }
  void reset() { *this = render_stats{}; }
};
} // namespace njin
