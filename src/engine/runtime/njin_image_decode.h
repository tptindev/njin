#pragma once
// Block-compressed images (DDS/KTX/PVR loaded by raylib's LoadImage) decoded to
// RGBA8 on the CPU with bimg, the image library pinned with bgfx. The legacy
// renderer uploads them compressed; the bgfx path has RGBA8 textures only.
#include "njin_internal_only.h"
#include "njin_rl.h"

namespace njin {
// Replaces a compressed image with its first level as RGBA8 and returns true.
// Returns false (image untouched) for uncompressed formats and in builds
// without bgfx; throws std::runtime_error for an unsupported compressed format.
bool image_decompress_rgba8(Image &image);
} // namespace njin
