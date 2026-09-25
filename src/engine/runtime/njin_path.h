#pragma once

#include <string>

namespace njin {
// Resolves an asset path the way every loader does: as given when it exists
// (relative to the working directory, or absolute), else relative to the
// folder holding the executable. The fallback is what lets a game started by
// double-clicking its exe, from a different working directory, still find
// the assets shipped next to it. Returns the path unchanged when neither
// exists, so the caller's "not found" message names what the game asked for.
std::string asset_path(const char *path);
} // namespace njin
