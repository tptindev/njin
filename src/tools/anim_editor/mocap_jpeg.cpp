// JPEG decoding for the Camera window. This raylib build has no JPEG
// (SUPPORT_FILEFORMAT_JPG is off), so the tool compiles its own private copy
// of the stb_image raylib bundles, static and JPEG only: no clash with
// raylib's PNG/BMP/... build of the same file.
#include "mocap.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#include <cstring>

namespace anim_editor {
bool decode_jpeg(const unsigned char *data, size_t size, std::vector<unsigned char> &rgb, int &width, int &height) {
  int w = 0, h = 0, channels = 0;
  unsigned char *pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 3);
  if (!pixels)
    return false;
  rgb.resize((size_t)w * (size_t)h * 3);
  std::memcpy(rgb.data(), pixels, rgb.size());
  stbi_image_free(pixels);
  width = w;
  height = h;
  return true;
}
} // namespace anim_editor
