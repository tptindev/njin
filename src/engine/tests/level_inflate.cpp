// Tiled's zlib and gzip tile layers are inflated with miniz (level_decompress, NJIN_BGFX):
// each layer must come back byte for byte, and equal to what raylib's DecompressData
// gave for the same raw DEFLATE stream while raylib is still built.
#include "njin_level_impl.h"
#include <miniz.h>
#include <raylib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what, size_t size, int level) {
  if (!ok) {
    std::printf("FAIL %s (size %zu, level %d)\n", what, size, level);
    ++failures;
  }
}

std::vector<njin::u8> zlib_stream(const std::vector<njin::u8> &raw, int level) {
  mz_ulong cap = mz_compressBound((mz_ulong)raw.size());
  std::vector<njin::u8> out(cap);
  if (mz_compress2(out.data(), &cap, raw.data(), (mz_ulong)raw.size(), level) != MZ_OK)
    std::abort();
  out.resize(cap);
  return out;
}

// gzip around the zlib stream's DEFLATE body, with a file name when asked (FNAME).
std::vector<njin::u8> gzip_stream(const std::vector<njin::u8> &zlib, bool name) {
  std::vector<njin::u8> out = {0x1f, 0x8b, 8, (njin::u8)(name ? 8 : 0), 0, 0, 0, 0, 0, 255};
  if (name)
    for (const char *c = "layer.bin"; ; ++c) {
      out.push_back((njin::u8)*c);
      if (*c == 0)
        break;
    }
  out.insert(out.end(), zlib.begin() + 2, zlib.end() - 4);
  for (int i = 0; i < 8; ++i)
    out.push_back(0); // CRC32 and ISIZE: not read
  return out;
}

} // namespace

int main() {
  std::mt19937 rng(1234);
  const size_t sizes[] = {1, 4, 64, 1000, 4096, 40000, 262144};
  int cases = 0;
  for (size_t size : sizes)
    for (int kind = 0; kind < 3; ++kind)
      for (int level : {0, 1, 6, 9}) {
        std::vector<njin::u8> raw(size);
        for (size_t i = 0; i < size; ++i) {
          if (kind == 0)
            raw[i] = (njin::u8)rng(); // noise: stored blocks
          else if (kind == 1)
            raw[i] = (i % 4 == 0) ? (njin::u8)(rng() % 6) : 0; // tile gids, little endian u32
          else
            raw[i] = (njin::u8)((i / 37) & 0xff); // long runs
        }
        const std::vector<njin::u8> zlib = zlib_stream(raw, level);
        std::vector<njin::u8> out;
        check(njin::level_decompress(zlib, "zlib", out) && out == raw, "zlib", size, level);
        for (bool name : {false, true}) {
          out.clear();
          check(njin::level_decompress(gzip_stream(zlib, name), "gzip", out) && out == raw, "gzip", size, level);
        }
        int rl_size = 0;
        unsigned char *rl = DecompressData(zlib.data() + 2, (int)zlib.size() - 2, &rl_size);
        check(rl != nullptr && rl_size == (int)raw.size() && std::memcmp(rl, raw.data(), raw.size()) == 0,
              "raylib DecompressData", size, level);
        MemFree(rl);
        cases += 3;
      }
  // Broken streams are refused, not crashed on.
  std::vector<njin::u8> out;
  const std::vector<njin::u8> garbage = {0x78, 0x9c, 0xff, 0xff, 0xff, 0xff, 0x00};
  check(!njin::level_decompress(garbage, "zlib", out), "garbage refused", garbage.size(), 0);
  const std::vector<njin::u8> not_gzip = {0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  check(!njin::level_decompress(not_gzip, "gzip", out), "bad gzip magic refused", not_gzip.size(), 0);
  std::printf("level_inflate: %d layers, %d failures\n", cases, failures);
  return failures == 0 ? 0 : 1;
}
