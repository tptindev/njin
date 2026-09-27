// Opens a folder in the system's file manager. Kept in its own file for the same reason as sysmon.cpp: the
// operating system headers clash with raylib's.
#pragma once
#include <string>

namespace inspector {
// Opens `dir` (Explorer, xdg-open, open). Only a folder that exists is opened: the path comes from the game, and
// handing an arbitrary path to the shell could start a program.
// Returns false when it is not a folder or the file manager could not be started.
bool open_folder(const std::string &dir);
} // namespace inspector
