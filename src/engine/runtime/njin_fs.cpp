#include "njin_fs.h"
#include <filesystem>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace njin {
const std::string &executable_dir() {
  static const std::string dir = [] {
    std::filesystem::path exe;
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
      const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
      if (n == 0) return std::string{};
      if (n < buffer.size()) { buffer.resize(n); break; }
      buffer.resize(buffer.size() * 2);
    }
    exe = buffer;
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) return std::string{};
    exe = buffer;
#else
    std::error_code error;
    exe = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return std::string{};
#endif
    const auto u8 = (exe.parent_path() / "").u8string();
    return std::string(u8.begin(), u8.end());
  }();
  return dir;
}

std::vector<std::uint8_t> base64_decode(std::string_view text) {
  const auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
  };
  std::vector<std::uint8_t> out;
  out.reserve(text.size() / 4 * 3);
  unsigned bits = 0;
  int count = 0;
  for (char c : text) {
    if (c == '=') break;
    const int v = value(c);
    if (v < 0) continue;
    bits = (bits << 6) | unsigned(v);
    count += 6;
    if (count >= 8) {
      count -= 8;
      out.push_back(std::uint8_t((bits >> count) & 0xff));
    }
  }
  return out;
}
} // namespace njin
