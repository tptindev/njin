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
