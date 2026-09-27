#pragma once
#include "njin_internal_only.h"

#include <string>

namespace njin {
// The OpenGL renderer and vendor strings, lower case. Needs a live context:
// call it after the window is open.
std::string gpu_renderer_name();
std::string gpu_vendor_name();

// True when the renderer names itself as a software rasterizer (llvmpipe,
// SwiftShader, the Windows "GDI Generic" fallback, ...). That is how a machine
// with no real GPU shows up, and there the features that spend GPU time to
// look better (particles on the GPU, text at window resolution) stay off.
// Probed once, on the first call.
bool gpu_is_software();
} // namespace njin
