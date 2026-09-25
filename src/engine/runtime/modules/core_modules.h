#pragma once

namespace njin {
struct njin_ctx;

// Registers the engine's built-in modules. Called by njin_create, so they run
// before any game module in every phase.
//
// To add one: create modules/<name>.cpp exposing `mod_desc <name>_module()`,
// then register it in core_modules.cpp.
void register_core_modules(njin_ctx &ctx);
} // namespace njin
