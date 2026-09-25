#include "njin_path.h"
#include <filesystem>
#include <raylib.h>

namespace njin {
std::string asset_path(const char *path) {
  if (path == nullptr)
    return {};
  if (FileExists(path))
    return path;
  const std::filesystem::path p = std::filesystem::path(reinterpret_cast<const char8_t *>(path));
  if (p.is_absolute())
    return path;
  const std::string next_to_exe = std::string(GetApplicationDirectory()) + path;
  if (FileExists(next_to_exe.c_str()))
    return next_to_exe;
  return path;
}
} // namespace njin
