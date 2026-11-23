#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>
// Image files without raylib (docs/renderer-legacy-removal.md, step 3): read with the
// standard library, decoded with bimg (pinned with bgfx: lodepng for PNG, stb for
// JPEG/TGA/BMP/GIF/PSD/HDR, tinyexr for EXR, its own DDS/KTX/PVR containers) and
// written with bimg (PNG, TGA) or the small encoders here (BMP, QOI). Grey images come
// out grey in every channel, as raylib's LoadImage + ImageFormat gave them.
namespace njin {
// The whole file, or false (bytes empty) when it cannot be read.
bool image_io_read_file(const char *path, std::vector<std::uint8_t> &bytes);
// Decoded to RGBA8 or RGBA32F (the first mip of the first layer); throws
// std::runtime_error naming `what` when the data is not an image bimg reads.
// `linear`, when given, says whether the file held floating point (linear) colour, as
// HDR and EXR do, rather than 8-bit sRGB.
RenderImageAsset image_io_decode(std::span<const std::uint8_t> bytes, RenderImageFormat format, const std::string &what,
                                 bool *linear = nullptr);
RenderImageAsset image_io_load(const char *path, RenderImageFormat format = RenderImageFormat::RGBA8, bool *linear = nullptr);
// RGBA8 pixels, top row first, by the path's extension: .png, .bmp, .tga or .qoi. False
// for any other extension (as raylib's ExportImage) or when the file cannot be written.
bool image_io_save(const char *path, unsigned width, unsigned height, std::span<const std::uint8_t> rgba);
}
