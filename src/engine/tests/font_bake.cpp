// The bgfx path's glyph atlas baked without raylib (font_bake_atlas) against raylib's
// LoadFontData + GenImageFontAtlas + ImageFormat, while the legacy renderer still has
// raylib: the default font and every .ttf under the given folders, at several pixel sizes,
// anti-aliased and pixel (FONT_BITMAP), must give the same glyphs, rectangles and atlas
// pixels.
#include "njin_font.h"
#include "njin_image_io.h"
#include <raylib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {
int failures = 0, cases = 0;
void compare(const std::string &name, const unsigned char *bytes, int length, int px, bool pixel, const std::vector<int> &cps) {
  ++cases;
  int count = 0;
  GlyphInfo *glyphs = LoadFontData(bytes, length, px, const_cast<int *>(cps.data()), int(cps.size()), pixel ? FONT_BITMAP : FONT_DEFAULT, &count);
  Rectangle *recs = nullptr;
  Image image = GenImageFontAtlas(glyphs, &recs, count, px, 4, 0);
  ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  Font mine{};
  mine.glyphPadding = 4;
  std::vector<unsigned char> rgba;
  const bool ok = njin::font_bake_atlas(bytes, pixel, px, cps, mine, rgba);
  std::string problem;
  if (!ok) problem = "not baked";
  else if (mine.glyphCount != count) problem = "glyph count " + std::to_string(mine.glyphCount) + " vs " + std::to_string(count);
  else if (mine.texture.width != image.width || mine.texture.height != image.height) problem = "atlas size differs";
  else {
    for (int i = 0; i < count && problem.empty(); ++i) {
      const auto &a = mine.glyphs[i], &b = glyphs[i];
      if (a.value != b.value || a.offsetX != b.offsetX || a.offsetY != b.offsetY || a.advanceX != b.advanceX ||
          a.image.width != b.image.width || a.image.height != b.image.height)
        problem = "glyph " + std::to_string(b.value) + " differs";
      else if (std::memcmp(&mine.recs[i], &recs[i], sizeof(Rectangle)) != 0)
        problem = "rectangle of glyph " + std::to_string(b.value) + " differs";
    }
    if (problem.empty() && std::memcmp(rgba.data(), image.data, rgba.size()) != 0) problem = "atlas pixels differ";
  }
  if (!problem.empty()) {
    std::printf("FAIL %s %dpx %s: %s\n", name.c_str(), px, pixel ? "pixel" : "smooth", problem.c_str());
    ++failures;
  }
  std::free(mine.glyphs);
  std::free(mine.recs);
  UnloadFontData(glyphs, count);
  MemFree(recs);
  UnloadImage(image);
}
} // namespace

int main(int argc, char **argv) {
  SetTraceLogLevel(LOG_ERROR);
  // Latin, Latin-1, Vietnamese, general punctuation and a CJK space: the sets games draw.
  std::vector<int> cps;
  for (int c = 32; c < 0x250; ++c) cps.push_back(c);
  for (int c = 0x1ea0; c < 0x1f00; ++c) cps.push_back(c);
  for (int c = 0x2000; c < 0x2070; ++c) cps.push_back(c);
  cps.push_back(0x3000);
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> fonts;
  fonts.push_back({"default", {njin::font_default_ttf, njin::font_default_ttf + njin::font_default_ttf_size}});
  for (int a = 1; a < argc; ++a) {
    if (!std::filesystem::is_directory(argv[a])) continue;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(argv[a])) {
      if (entry.path().extension() != ".ttf") continue;
      std::vector<std::uint8_t> bytes;
      if (njin::image_io_read_file(entry.path().string().c_str(), bytes)) fonts.push_back({entry.path().filename().string(), bytes});
    }
  }
  for (const auto &[name, bytes] : fonts)
    for (int px : {8, 13, 16, 24, 40, 96})
      for (bool pixel : {false, true}) compare(name, bytes.data(), int(bytes.size()), px, pixel, cps);
  std::printf("font_bake: %zu fonts, %d atlases, %d differ\n", fonts.size(), cases, failures);
  return failures == 0 ? 0 : 1;
}
