#pragma once

/// @addtogroup grp_core
/// @{

/// njin's version number, following semver: `MAJOR.MINOR.PATCH`.
///
/// This is the **single source** of the version number: CMake reads these very
/// `#define` lines (see `CMakeLists.txt`), and the startup log and njin_inspector both
/// show this number. To release, edit the three numbers here, add an entry to `CHANGELOG.md`, then
/// create the tag `vMAJOR.MINOR.PATCH`.
///
/// - **MAJOR**: a big change. When this number goes up, the engine's core changes and
///   may be incompatible with older versions: a game written for the old API may need
///   edits.
/// - **MINOR**: new features. The engine gains features but stays
///   compatible with older versions of the same MAJOR line: existing games still build and
///   run as before.
/// - **PATCH**: bug fixes. Only fixes and security patches, no new features.
///
/// Bumping a number resets the numbers to its right to 0 (`0.2.3` becomes `0.3.0`). While MAJOR
/// is still 0, the API is not stable, so a MINOR release may still change the API; `CHANGELOG.md` will
/// say so clearly when that happens.
#define NJIN_VERSION_MAJOR 0 ///< MAJOR number: raised for a big change, may be incompatible with older versions.
#define NJIN_VERSION_MINOR 5 ///< MINOR number: raised when new features are added, still compatible within the line.
#define NJIN_VERSION_PATCH 0 ///< PATCH number: raised for bug and security fixes only.

/// The version as a number for comparison: `MAJOR * 10000 + MINOR * 100 + PATCH`.
/// For example 0.1.0 is 100. Use `#if NJIN_VERSION >= 200` to support several engine versions.
#define NJIN_VERSION (NJIN_VERSION_MAJOR * 10000 + NJIN_VERSION_MINOR * 100 + NJIN_VERSION_PATCH)

/// @}

namespace njin {
/// @addtogroup grp_core
/// @{

/// Version of the running engine, as the string `"0.1.0"`.
///
/// Unlike the macros above, this function reports the version of the **linked library**,
/// not the header the game was compiled against: two different numbers mean the game
/// and the engine were built from two different versions.
/// @return A static string.
const char *version();
/// @}
} // namespace njin
