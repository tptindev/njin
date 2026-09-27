#include "os_open.h"
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace inspector {
bool open_folder(const std::string &dir) {
  if (dir.empty())
    return false;
  const std::filesystem::path p(reinterpret_cast<const char8_t *>(dir.c_str()));
  std::error_code ec;
  if (!std::filesystem::is_directory(p, ec))
    return false;
#if defined(_WIN32)
  const HINSTANCE r = ShellExecuteW(nullptr, L"explore", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(r) > 32;
#else
#if defined(__APPLE__)
  const char *opener = "open";
#else
  const char *opener = "xdg-open";
#endif
  const std::string path = p.string();
  // Forked twice so the file manager is not our child: nothing is left to reap when it closes.
  const pid_t pid = fork();
  if (pid < 0)
    return false;
  if (pid == 0) {
    if (fork() == 0) {
      // No shell in between: the path is one argument, whatever characters it holds.
      execlp(opener, opener, path.c_str(), static_cast<char *>(nullptr));
      _exit(127);
    }
    _exit(0);
  }
  waitpid(pid, nullptr, 0);
  return true;
#endif
}
} // namespace inspector
