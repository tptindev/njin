#pragma once
#include "njin_internal_only.h"

#include "_mod.h"
#include "_types.h"
#include "njin_video.h"
#include <memory>
#include <raylib.h>
#include <vector>

struct plm_t;

namespace njin {
// Videos (njin_video.h), decoded by pl_mpeg. Same handle rules as the other
// stores: id N maps to slots[N - 1], id 0 is invalid, slots are never reused.
// A video's texture lives in the texture store, so draws find it like any
// other; video_close unloads it there.

struct video_slot {
  bool alive = false;
  plm_t *plm = nullptr;
  texture_handle texture{};
  i32 width = 0, height = 0;
  std::vector<unsigned char> pixels; // RGBA of the newest frame
  bool fresh = false;                // pixels newer than the texture
  i32 frames = 0;                    // frames shown since open
  bool playing = false;
  bool finished = false;
  bool loop = false;
  // Sound: pl_mpeg pushes interleaved stereo samples into `ring`, and the
  // stream takes them a buffer at a time; an empty ring plays silence.
  bool has_audio = false;
  AudioStream stream{};
  i32 stream_frames = 0; // frames per stream buffer
  std::vector<f32> ring;
  usize ring_read = 0, ring_count = 0; // in floats
  audio_bus bus = bus_music;
  f32 volume = 1.0f;
};

struct video_store {
  // Boxed: pl_mpeg's callbacks hold a pointer to their slot.
  std::vector<std::unique_ptr<video_slot>> videos;

  video_store() = default;
  ~video_store();
  video_store(const video_store &) = delete;
  video_store &operator=(const video_store &) = delete;
};

// Core module: decodes every playing video in phase_post_update.
mod_desc video_module();
} // namespace njin
