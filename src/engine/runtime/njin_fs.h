#pragma once
#include "njin_internal_only.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
// File-system helpers without raylib (docs/renderer-legacy-removal.md, step 3), beside
// the public file_exists/file_read (njin_file.h). Paths are UTF-8.
namespace njin {
// The folder of the running executable, ending in a separator (raylib's
// GetApplicationDirectory); empty when the OS does not say.
const std::string &executable_dir();
// Standard or URL-safe base64 (as in a data: URI); stops at '=' or the end and skips
// what is not in the alphabet.
std::vector<std::uint8_t> base64_decode(std::string_view text);
} // namespace njin
