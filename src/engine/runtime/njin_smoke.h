#pragma once
#include "njin_internal_only.h"
#include "njin_cfg.h"
// Smoke runs of a whole game for the test suite, without changing the game: only when
// these environment variables are set (nothing happens otherwise).
//   NJIN_SMOKE_BACKEND     backend name as parse_graphics_backend takes it (vulkan, d3d12,
//                          bgfx-opengl, opengl...), in place of config::graphics_backend
//   NJIN_SMOKE_FRAMES      quit after this many rendered frames
//   NJIN_SMOKE_SCREENSHOT  path: capture a frame near the end; at shutdown the capture is
//                          checked and "NJIN_SMOKE ok ..." or "NJIN_SMOKE blank ..." logged
namespace njin {
struct context;
config smoke_config(const config &cfg);
void smoke_install(context &ctx);
}
