#pragma once
#include "../njin_internal_only.h"

namespace njin {
struct context;

// Registers the engine's built-in modules. Called by create, so they run
// before any game module in every phase.
//
// To add one: create modules/<name>.cpp exposing `mod_desc <name>_module()`,
// then register it in core_modules.cpp.
void register_core_modules(context &ctx);
} // namespace njin
