#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"

namespace njin {
// Core module. In phase_post_update, after the hierarchy module and before
// the sprite module prepares tilemap chunks for this frame's view, moves every
// camera that has a camera_follow toward its target and clamps it to its
// bounds.
mod_desc camera_follow_module();
} // namespace njin
