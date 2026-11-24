#include "njin_path.h"
#include "njin_file.h"
#include "njin_fs.h"
#include <filesystem>

namespace njin {
std::string asset_path(const char *path) {
  if (path == nullptr)
    return {};
  if (file_exists(path))
    return path;
  const std::filesystem::path p = std::filesystem::path(reinterpret_cast<const char8_t *>(path));
  if (p.is_absolute())
    return path;
  const std::string next_to_exe = executable_dir() + path;
  if (file_exists(next_to_exe.c_str()))
    return next_to_exe;
  return path;
}
} // namespace njin
