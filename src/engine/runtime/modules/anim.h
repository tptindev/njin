#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"

namespace njin {
// Core module. In phase_post_update, for every entity with an animator and a
// sprite: follows the first matching graph transition, advances the clip by
// delta() and writes sprite.texture and sprite.source. Registered before the
// sprite module so the frame drawn is this frame's.
mod_desc anim_module();
} // namespace njin
