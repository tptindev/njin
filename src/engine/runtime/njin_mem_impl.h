#pragma once
#include "njin_internal_only.h"

namespace njin {
struct context;
struct config;

// Applies config::mem_budget_mb and mem_limit_mb. Called from create().
void mem_apply_config(const config &cfg);
// Start of every frame: runs the mem_on_pressure() callbacks when the soft
// limit was crossed, or mem_pressure() asked for them.
void mem_frame(context &ctx);
} // namespace njin
