#pragma once
#include "_mod.h"

namespace njin {
// Core module. In phase_pre_update, copies actions into the bodies that have
// an input map. In phase_fixed_update, moves path_movers first (carrying what
// stands on them), then steps every platformer_body and topdown_body through
// collision_move. It runs before the game's modules, so a game's fixed
// systems see this step's positions.
mod_desc body_module();
} // namespace njin
