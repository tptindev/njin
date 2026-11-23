// njin_image_io (bimg) against raylib, while the legacy renderer still has raylib: every
// image under the given folders decodes to the same RGBA8 pixels both ways (PNG/BMP/TGA/
// GIF exactly, JPEG within 2 per channel), and what image_io_save writes as PNG, BMP,
// TGA and QOI reads back unchanged (PNG and QOI through raylib).
#include "njin_image_io.h"
#include "njin_internal_only.h"
#include <raylib.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> raylib_rgba(const std::string &path, int &w, int &h) {
  Image image = LoadImage(path.c_str());
  std::vector<std::uint8_t> out;
  if (!image.data) return out;
  ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  w = image.width;
  h = image.height;
  const auto *p = static_cast<const std::uint8_t *>(image.data);
  out.assign(p, p + size_t(w) * h * 4);
  UnloadImage(image);
  return out;
}
} // namespace

int main(int argc, char **argv) {
  SetTraceLogLevel(LOG_WARNING);
  int checked = 0, failed = 0, skipped = 0;
  for (int a = 1; a < argc; ++a) {
    if (!std::filesystem::is_directory(argv[a])) continue;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(argv[a])) {
      if (!entry.is_regular_file()) continue;
      auto ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".bmp" && ext != ".tga" && ext != ".gif" && ext != ".hdr") continue;
      const auto path = entry.path().string();
      int w = 0, h = 0;
      const auto reference = raylib_rgba(path, w, h);
      if (reference.empty()) { ++skipped; continue; } // raylib cannot read it either
      ++checked;
      try {
        const auto image = njin::image_io_load(path.c_str());
        if (int(image.width) != w || int(image.height) != h) {
          std::printf("FAIL %s: %ux%u vs raylib %dx%d\n", path.c_str(), image.width, image.height, w, h);
          ++failed;
          continue;
        }
        int worst = 0;
        size_t differ = 0;
        for (size_t i = 0; i < reference.size(); ++i) {
          const int d = std::abs(int(reference[i]) - int(image.pixels[i]));
          worst = std::max(worst, d);
          differ += d != 0;
        }
        const int allowed = ext == ".jpg" || ext == ".jpeg" || ext == ".hdr" ? 2 : 0;
        if (worst > allowed) {
          std::printf("FAIL %s: %zu channel values differ, worst %d\n", path.c_str(), differ, worst);
          ++failed;
        }
      } catch (const std::exception &e) {
        std::printf("FAIL %s: %s\n", path.c_str(), e.what());
        ++failed;
      }
    }
  }
  // Writing: a small gradient with alpha through every encoder, read back by raylib.
  std::vector<std::uint8_t> pixels(37 * 23 * 4);
  for (size_t i = 0; i < 37 * 23; ++i) {
    pixels[i * 4] = std::uint8_t(i * 7);
    pixels[i * 4 + 1] = std::uint8_t(i / 3);
    pixels[i * 4 + 2] = std::uint8_t(255 - i % 256);
    pixels[i * 4 + 3] = std::uint8_t(i % 5 == 0 ? 0 : 128 + i % 128);
  }
  const auto dir = std::filesystem::temp_directory_path() / "njin_image_io_test";
  std::filesystem::create_directories(dir);
  for (const char *ext : {".png", ".bmp", ".tga", ".qoi"}) {
    const auto path = (dir / (std::string("roundtrip") + ext)).string();
    std::filesystem::remove(path);
    ++checked;
    if (!njin::image_io_save(path.c_str(), 37, 23, pixels)) {
      std::printf("FAIL save %s\n", ext);
      ++failed;
      continue;
    }
    int w = 0, h = 0;
    // raylib here is built without BMP/TGA reading: those go through bimg's stb reader,
    // which shares no code with the encoders.
    std::vector<std::uint8_t> back;
    if (std::string(ext) == ".bmp" || std::string(ext) == ".tga") {
      const auto image = njin::image_io_load(path.c_str());
      w = int(image.width);
      h = int(image.height);
      back = image.pixels;
    } else {
      back = raylib_rgba(path, w, h);
    }
    if (w != 37 || h != 23 || back != pixels) {
      std::printf("FAIL roundtrip %s: %dx%d, %s\n", ext, w, h, back == pixels ? "same pixels" : "pixels differ");
      ++failed;
    }
  }
  std::printf("image_io: %d checked, %d failed, %d raylib cannot read\n", checked, failed, skipped);
  return failed == 0 && checked > 4 ? 0 : 1;
}
