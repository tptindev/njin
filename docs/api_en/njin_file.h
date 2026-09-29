#pragma once
#include "_types.h"
#include <string>
#include <string_view>

namespace njin {
struct context;

/// @addtogroup grp_file
/// @{

/// Whether a file exists.
/// @param path Path.
/// @return `true` if it is an existing file (not a directory).
bool file_exists(const char *path);

/// Reads a whole file.
/// @param path Path.
/// @param out Receives the file contents. Unchanged if the read fails.
/// @return `true` if the file was read.
bool file_read(const char *path, std::string &out);

/// Writes a whole file, creating the parent folder if it is missing.
///
/// Writes to a temporary file and then renames it, so if the game quits halfway
/// the old file is still intact, never half-written. Use it for save files.
/// @param path Path.
/// @param data Contents, may be binary data.
/// @return `true` if the file was written.
bool file_write(const char *path, std::string_view data);

/// Full path of a save file, inside the user's own folder.
///
/// The folder is `%APPDATA%/<game name>` on Windows,
/// `~/Library/Application Support/<game name>` on macOS and
/// `~/.local/share/<game name>` on Linux. The game name comes from `config::app_name`,
/// or from the window title if that is empty. The folder is created if missing.
/// @param ctx Engine context.
/// @param file_name File name, for example `"save.txt"`.
/// @return Full path.
std::string save_path(const context &ctx, const char *file_name);
/// @}
} // namespace njin
