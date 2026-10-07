#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"

namespace njin {
// Core module. Every frame, in phase_post_update, updates 3D voices (listener,
// attached entities, volume/pan/pitch), restarts looping sounds that have
// ended and feeds the music streams. Without it music falls silent after the
// first buffer and sound_play_loop plays once.
mod_desc audio_module();
} // namespace njin
