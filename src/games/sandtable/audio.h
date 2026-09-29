#pragma once

#include <njin.h>

namespace sandtable {
using namespace njin;

enum class sfx_type {
  click,
  drum,
  slash,
  cannon,
  charge,
  chip,
  horn,
  victory,
  defeat,
  arrow,
  trumpet,
  spear,
  explosion,
  count
};

void audio_init(context &ctx);
void audio_play(context &ctx, sfx_type type, f32 volume = 1.0f);
// Plays unless the same sound played less than `gap` seconds ago: a battle of
// thousands must not play thousands of clashes a second.
void audio_play_gated(context &ctx, sfx_type type, f32 volume, f32 gap);
sound_handle audio_sound(sfx_type type);

} // namespace sandtable
