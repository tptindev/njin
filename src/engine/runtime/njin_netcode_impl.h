#pragma once
#include "njin_internal_only.h"

#include "_mod.h"

namespace njin {
// Netcode (njin_netcode.h): ticks, inputs, tick sync, snapshot interpolation
// and component replication, on top of net_host (njin_multiplayer.h) and
// bit_writer. One session per context.

struct netcode_state; // njin_netcode.cpp

void netcode_state_destroy(netcode_state *state);

// Core module: receives in phase_pre_update and at the start of every fixed
// step (where the tick advances), sends in phase_post_update.
mod_desc netcode_module();
} // namespace njin
