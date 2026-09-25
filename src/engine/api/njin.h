#pragma once
#include "njin_cfg.h"

namespace njin {
// Opaque engine handle. Created by njin_create, released by njin_destroy.
struct njin_ctx;

// Opens the window and returns the engine context. Never returns nullptr.
njin_ctx *njin_create(const njin_cfg &cfg);
void njin_run(njin_ctx &ctx);
// Releases every engine resource and closes the window. nullptr is ignored.
void njin_destroy(njin_ctx *ctx);
} // namespace njin
