#pragma once

#include <njin.h>

namespace xiangqi {
using namespace njin;

enum class sfx_type {
  click,
  command,
  slash,
  cannon,
  charge,
  river_cross,
  rally,
  victory,
  defeat
};

void audio_init(context &ctx);
void audio_play(context &ctx, sfx_type type, f32 volume = 1.0f);
sound_handle audio_sound(sfx_type type);

} // namespace xiangqi
