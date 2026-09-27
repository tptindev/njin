#pragma once
#include "../njin_internal_only.h"
#include "_mod.h"

namespace njin {
// Core module. In phase_post_update, writes the transform of every child_of
// entity from its parent's transform and its `local`, parents before
// children, and destroys (or detaches) children whose parent is gone. Runs
// before the anim, particle and sprite modules so they all see this frame's
// positions.
mod_desc hierarchy_module();
} // namespace njin
