#pragma once

#include "_types.h"
#include "njin_audio.h"
#include "njin_cfg.h"
#include "njin_ecs.h"
#include "njin_input.h"
#include "njin_shader.h"
#include "njin_texture.h"

namespace njin {
// Opens the window on construction and closes it on destruction.
struct window_guard {
  explicit window_guard(const njin_cfg &cfg);
  ~window_guard();
  window_guard(const window_guard &) = delete;
  window_guard &operator=(const window_guard &) = delete;
};

// Definition of the opaque njin_ctx handle. Only the runtime sees this.
//
// Members are destroyed in reverse order, so `window` (declared before the
// stores) closes last: GPU resources in `shader`, `texture` and
// `render_texture`, and the audio buffers in `audio`, are freed while the GL
// context and the audio device are still alive.
struct njin_ctx {
  explicit njin_ctx(const njin_cfg &config) : cfg(config), window(config) {}

  f32 dt = 0.0f;
  f32 elapsed = 0.0f;
  njin_cfg cfg;
  window_guard window;
  input_store input;
  shader_store shader;
  texture_store texture;
  render_texture_store render_texture;
  audio_store audio;
  ecs_store ecs;
};
} // namespace njin
