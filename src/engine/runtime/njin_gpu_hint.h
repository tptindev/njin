#pragma once
#include "njin_internal_only.h"

namespace njin {
// Asks the system to run the game on the discrete GPU of a laptop that has two,
// instead of the integrated one it would start on. Called by njin_create()
// before the window (and so the OpenGL context) exists, which is the only time
// it can still have an effect.
//
// Windows: nothing here. The .exe must export the variables NVIDIA Optimus and
// AMD PowerXpress look for, which only works from the .exe itself: that is
// njin::gpu, linked by each game (src/engine/gpu/gpu_preference.cpp).
// Linux: on a laptop, sets the render-offload environment variables of Mesa
// (DRI_PRIME) and of the NVIDIA driver, unless the user already set them. Not on
// a desktop: there the monitor is normally on the discrete card, and "offload"
// would move the game to the integrated one.
// macOS: nothing to do. A Mac with two GPUs uses the discrete one for any app
// that does not opt in to automatic graphics switching, and njin does not.
//
// Switched off by the NJIN_PREFER_DISCRETE_GPU option in CMake.
void prefer_discrete_gpu();
} // namespace njin
