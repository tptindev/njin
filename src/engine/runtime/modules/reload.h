#pragma once
#include "_mod.h"
#include "njin_filetime.h"
#include "njin_reload.h"
#include <string>
#include <unordered_map>

namespace njin {
// Core module. In phase_pre_update, while hot reload is on, checks the
// modification time of every texture and shader file every `interval` real
// seconds and reloads the ones that changed into their existing handles.
mod_desc reload_module();

struct reload_watch {
  file_stamp seen{};    // stamp of the version in use
  file_stamp pending{}; // changed stamp waiting to settle
  bool has_pending = false;
};

struct reload_state {
  bool enabled = false;
  f32 interval = 0.25f;
  f32 timer = 0.0f;
  std::unordered_map<std::string, reload_watch> watches; // by file path
};
} // namespace njin
