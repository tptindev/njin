#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include <string>

namespace njin {
// What changes when a file is saved: its modification time at full
// resolution, and its size. std::filesystem on MinGW reports times in whole
// seconds, which misses two saves within one second, so Windows asks Win32.
struct file_stamp {
  u64 time = 0;
  u64 size = 0;
  bool operator==(const file_stamp &) const = default;
};

// Reads the stamp of `path` (UTF-8). False if the file cannot be read now,
// e.g. while an editor is replacing it.
bool file_stamp_of(const std::string &path, file_stamp &out);
} // namespace njin
