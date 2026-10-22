#pragma once
#include "njin_internal_only.h"

#include "_mod.h"

namespace njin {
// Lockstep (njin_lockstep.h): a host that orders every player's commands into
// ticks, guests that run the ticks it has closed, state hashes compared, on
// top of net_host (njin_multiplayer.h). One session per context.

struct lockstep_state; // njin_lockstep.cpp

void lockstep_state_destroy(lockstep_state *state);

// Core module: receives in phase_pre_update (and in lockstep_step), sends in
// phase_post_update.
mod_desc lockstep_module();
} // namespace njin
