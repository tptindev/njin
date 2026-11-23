#include "njin_image_io.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#if NJIN_BGFX
#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/file.h>
#else
#include <raylib.h>
#endif

namespace njin {
namespace {
std::filesystem::path utf8_path(const char *path) {
  return std::filesystem::path(reinterpret_cast<const char8_t *>(path));
}
std::string lower_extension(const char *path) {
  auto ext = utf8_path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return ext;
}
bool write_file(const char *path, const std::vector<std::uint8_t> &bytes) {
  std::ofstream out(utf8_path(path), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
  return bool(out);
}
void put16(std::vector<std::uint8_t> &out, unsigned v) { out.push_back(std::uint8_t(v)); out.push_back(std::uint8_t(v >> 8)); }
void put32(std::vector<std::uint8_t> &out, unsigned v) { put16(out, v & 0xffff); put16(out, v >> 16); }

// 32-bit BMP with a BITMAPV4HEADER (masks, sRGB), bottom-up, so the alpha survives.
std::vector<std::uint8_t> encode_bmp(unsigned w, unsigned h, std::span<const std::uint8_t> rgba) {
  std::vector<std::uint8_t> out;
  const unsigned header = 14 + 108, size = header + w * h * 4;
  out.push_back('B'); out.push_back('M'); put32(out, size); put32(out, 0); put32(out, header);
  put32(out, 108); put32(out, w); put32(out, h); put16(out, 1); put16(out, 32); put32(out, 3); put32(out, w * h * 4);
  put32(out, 2835); put32(out, 2835); put32(out, 0); put32(out, 0);
  put32(out, 0x00ff0000); put32(out, 0x0000ff00); put32(out, 0x000000ff); put32(out, 0xff000000);
  put32(out, 0x73524742); // 'sRGB'
  for (int i = 0; i < 12; ++i) put32(out, 0); // endpoints (9) and gamma (3), unused with sRGB
  for (unsigned y = h; y-- > 0;)
    for (unsigned x = 0; x < w; ++x) {
      const auto *p = &rgba[(size_t(y) * w + x) * 4];
      out.insert(out.end(), {p[2], p[1], p[0], p[3]});
    }
  return out;
}

// QOI (qoiformat.org, specification 1.0), RGBA, sRGB tag.
std::vector<std::uint8_t> encode_qoi(unsigned w, unsigned h, std::span<const std::uint8_t> rgba) {
  std::vector<std::uint8_t> out{'q', 'o', 'i', 'f'};
  for (unsigned v : {w, h}) for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(v >> s));
  out.push_back(4); out.push_back(0);
  std::uint8_t index[64][4]{};
  std::uint8_t prev[4]{0, 0, 0, 255};
  unsigned run = 0;
  const size_t count = size_t(w) * h;
  for (size_t i = 0; i < count; ++i) {
    const std::uint8_t *px = &rgba[i * 4];
    if (std::memcmp(px, prev, 4) == 0) {
      if (++run == 62 || i + 1 == count) { out.push_back(std::uint8_t(0xc0 | (run - 1))); run = 0; }
      continue;
    }
    if (run) { out.push_back(std::uint8_t(0xc0 | (run - 1))); run = 0; }
    const unsigned slot = (px[0] * 3 + px[1] * 5 + px[2] * 7 + px[3] * 11) % 64;
    if (std::memcmp(index[slot], px, 4) == 0) {
      out.push_back(std::uint8_t(slot));
    } else {
      std::memcpy(index[slot], px, 4);
      if (px[3] == prev[3]) {
        const int dr = int(px[0]) - prev[0], dg = int(px[1]) - prev[1], db = int(px[2]) - prev[2];
        const auto wrap = [](int d) { return int(std::int8_t(std::uint8_t(d))); };
        const int vr = wrap(dr), vg = wrap(dg), vb = wrap(db), vgr = vr - vg, vgb = vb - vg;
        if (vr > -3 && vr < 2 && vg > -3 && vg < 2 && vb > -3 && vb < 2)
          out.push_back(std::uint8_t(0x40 | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2)));
        else if (vgr > -9 && vgr < 8 && vg > -33 && vg < 32 && vgb > -9 && vgb < 8) {
          out.push_back(std::uint8_t(0x80 | (vg + 32)));
          out.push_back(std::uint8_t((vgr + 8) << 4 | (vgb + 8)));
        } else out.insert(out.end(), {0xfe, px[0], px[1], px[2]});
      } else out.insert(out.end(), {0xff, px[0], px[1], px[2], px[3]});
    }
    std::memcpy(prev, px, 4);
  }
  out.insert(out.end(), {0, 0, 0, 0, 0, 0, 0, 1});
  return out;
}
} // namespace

bool image_io_read_file(const char *path, std::vector<std::uint8_t> &bytes) {
  bytes.clear();
  if (!path || !*path) return false;
  std::ifstream in(utf8_path(path), std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  const auto size = in.tellg();
  if (size <= 0) return false;
  bytes.resize(size_t(size));
  in.seekg(0);
  in.read(reinterpret_cast<char *>(bytes.data()), size);
  if (!in) { bytes.clear(); return false; }
  return true;
}

RenderImageAsset image_io_decode(std::span<const std::uint8_t> bytes, RenderImageFormat format, const std::string &what, bool *linear) {
  if (format != RenderImageFormat::RGBA8 && format != RenderImageFormat::RGBA32F)
    throw std::invalid_argument("njin image: decode to RGBA8 or RGBA32F");
  if (bytes.empty() || bytes.size() > 0x7fffffff) throw std::runtime_error(what + ": no image data");
#if NJIN_BGFX
  bx::DefaultAllocator allocator;
  bx::Error error;
  bimg::ImageContainer *image = bimg::imageParse(&allocator, bytes.data(), uint32_t(bytes.size()), bimg::TextureFormat::Count, &error);
  if (!image) throw std::runtime_error(what + ": cannot be decoded (" + std::string(error.getMessage().getCPtr(), error.getMessage().getLength()) + ")");
  struct Free { bimg::ImageContainer *image; ~Free() { bimg::imageFree(image); } } free{image};
  bimg::ImageMip mip;
  if (!bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip))
    throw std::runtime_error(what + ": has no image data");
  const unsigned w = mip.m_width, h = mip.m_height;
  if (linear) {
    using F = bimg::TextureFormat;
    const auto f = mip.m_format;
    *linear = f == F::R16F || f == F::RG16F || f == F::RGBA16F || f == F::R32F || f == F::RG32F || f == F::RGBA32F ||
              f == F::RG11B10F || f == F::RGB9E5F || f == F::BC6H;
  }
  if (w == 0 || h == 0 || w > 16384 || h > 16384) throw std::runtime_error(what + ": dimensions must be in [1,16384]");
  RenderImageAsset result;
  result.width = w; result.height = h; result.format = format;
  const size_t count = size_t(w) * h;
  const bool rgba8 = format == RenderImageFormat::RGBA8;
  result.pixels.resize(count * (rgba8 ? 4 : 16));
  // Grey and grey-with-alpha: the grey in every colour channel (bimg would put it in red).
  const auto src = mip.m_format;
  const bool grey8 = src == bimg::TextureFormat::R8 || src == bimg::TextureFormat::RG8;
  const bool grey16 = src == bimg::TextureFormat::R16 || src == bimg::TextureFormat::RG16;
  if (grey8 || grey16) {
    const unsigned channels = src == bimg::TextureFormat::RG8 || src == bimg::TextureFormat::RG16 ? 2 : 1;
    for (size_t i = 0; i < count; ++i) {
      float grey, alpha = 1;
      if (grey8) {
        grey = mip.m_data[i * channels] / 255.0f;
        if (channels == 2) alpha = mip.m_data[i * 2 + 1] / 255.0f;
      } else {
        std::uint16_t v[2]{};
        std::memcpy(v, mip.m_data + i * channels * 2, channels * 2);
        grey = v[0] / 65535.0f;
        if (channels == 2) alpha = v[1] / 65535.0f;
      }
      if (rgba8) {
        const auto g = std::uint8_t(grey * 255 + 0.5f), a = std::uint8_t(alpha * 255 + 0.5f);
        std::uint8_t *out = &result.pixels[i * 4];
        out[0] = out[1] = out[2] = g; out[3] = a;
      } else {
        const float px[4]{grey, grey, grey, alpha};
        std::memcpy(&result.pixels[i * 16], px, 16);
      }
    }
    return result;
  }
  const auto dst = rgba8 ? bimg::TextureFormat::RGBA8 : bimg::TextureFormat::RGBA32F;
  if (src == dst) {
    std::memcpy(result.pixels.data(), mip.m_data, result.pixels.size());
  } else if (bimg::isCompressed(src)) {
    if (!rgba8) throw std::runtime_error(what + ": a compressed image cannot be read as floating point");
    bimg::imageDecodeToRgba8(&allocator, result.pixels.data(), mip.m_data, w, h, w * 4, src);
  } else if (!bimg::imageConvert(&allocator, result.pixels.data(), dst, mip.m_data, src, w, h, 1)) {
    throw std::runtime_error(what + ": unsupported pixel format");
  }
  return result;
#else
  const auto ext = what.find(".jpg") != std::string::npos || what.find(".jpeg") != std::string::npos ? ".jpg" : ".png";
  Image image = LoadImageFromMemory(ext, bytes.data(), int(bytes.size()));
  if (!image.data) throw std::runtime_error(what + ": cannot be decoded");
  if (linear) *linear = image.format >= PIXELFORMAT_UNCOMPRESSED_R32 && image.format <= PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
  ImageFormat(&image, format == RenderImageFormat::RGBA8 ? PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 : PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
  RenderImageAsset result{unsigned(image.width), unsigned(image.height), {}, format};
  const auto *p = static_cast<const std::uint8_t *>(image.data);
  result.pixels.assign(p, p + size_t(image.width) * image.height * (format == RenderImageFormat::RGBA8 ? 4 : 16));
  UnloadImage(image);
  return result;
#endif
}

RenderImageAsset image_io_load(const char *path, RenderImageFormat format, bool *linear) {
  if (!path || !*path) throw std::invalid_argument("njin image: empty path");
  std::vector<std::uint8_t> bytes;
  if (!image_io_read_file(path, bytes)) throw std::runtime_error(std::string("njin image: cannot read ") + path);
  return image_io_decode(bytes, format, std::string("njin image ") + path, linear);
}

bool image_io_save(const char *path, unsigned width, unsigned height, std::span<const std::uint8_t> rgba) {
  if (!path || !*path || width == 0 || height == 0 || rgba.size() < size_t(width) * height * 4) return false;
  const auto ext = lower_extension(path);
  if (ext == ".bmp") return write_file(path, encode_bmp(width, height, rgba));
  if (ext == ".qoi") return write_file(path, encode_qoi(width, height, rgba));
  if (ext != ".png" && ext != ".tga") return false;
#if NJIN_BGFX
  bx::FileWriter writer;
  bx::Error error;
  if (!bx::open(&writer, bx::FilePath(path), false, &error)) return false;
  if (ext == ".tga") {
    std::vector<std::uint8_t> bgra(rgba.begin(), rgba.begin() + std::ptrdiff_t(size_t(width) * height * 4));
    for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    bimg::imageWriteTga(&writer, width, height, width * 4, bgra.data(), false, false, &error);
  } else {
    bimg::imageWritePng(&writer, width, height, width * 4, rgba.data(), bimg::TextureFormat::RGBA8, false, &error);
  }
  bx::close(&writer);
  return error.isOk();
#else
  Image view{const_cast<std::uint8_t *>(rgba.data()), int(width), int(height), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  return ExportImage(view, path);
#endif
}
} // namespace njin
