// Kept apart from the rest of the runtime: <windows.h> and <raylib.h> declare
// clashing names (CloseWindow, Rectangle, ...) and cannot share a file.
#include "njin_filetime.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <filesystem>
#include <system_error>
#endif

namespace njin {
bool file_stamp_of(const std::string &path, file_stamp &out) {
#if defined(_WIN32)
  const int wide_len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
  if (wide_len <= 0)
    return false;
  std::wstring wide((usize)wide_len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), wide_len);
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &data))
    return false;
  out.time = ((u64)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
  out.size = ((u64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
  return true;
#else
  namespace fs = std::filesystem;
  const fs::path p(reinterpret_cast<const char8_t *>(path.c_str()));
  std::error_code ec;
  const auto time = fs::last_write_time(p, ec);
  if (ec)
    return false;
  const auto size = fs::file_size(p, ec);
  if (ec)
    return false;
  out.time = (u64)time.time_since_epoch().count();
  out.size = (u64)size;
  return true;
#endif
}
} // namespace njin
