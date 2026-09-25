#include "njin_file.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace njin {
namespace fs = std::filesystem;

namespace {
// Paths in njin are UTF-8. On Windows a plain `char` path would be read in
// the ANSI code page, so they go through char8_t to keep accented names.
fs::path to_path(const char *path) {
  return fs::path(reinterpret_cast<const char8_t *>(path));
}

// Folder names cannot hold some characters on Windows; a title like
// "Pong: the game" would otherwise produce a path that cannot be created.
std::string folder_name(const char *name) {
  std::string out;
  for (const char *c = name; c != nullptr && *c != '\0'; c++) {
    const char ch = *c;
    const bool bad = ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
                     ch == '/' || ch == '\\' || ch == '|' || ch == '?' ||
                     ch == '*' || (unsigned char)ch < 32;
    out.push_back(bad ? '_' : ch);
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
    out.pop_back();
  return out.empty() ? std::string("njin") : out;
}

fs::path user_data_dir() {
#if defined(_WIN32)
  // The wide variant: the narrow one is in the ANSI code page, which would
  // mangle a user name with accents.
  if (const wchar_t *appdata = _wgetenv(L"APPDATA"))
    return fs::path(appdata);
#elif defined(__APPLE__)
  if (const char *home = std::getenv("HOME"))
    return to_path(home) / "Library" / "Application Support";
#else
  if (const char *xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg)
    return to_path(xdg);
  if (const char *home = std::getenv("HOME"))
    return to_path(home) / ".local" / "share";
#endif
  return fs::current_path();
}
} // namespace

bool file_exists(const char *path) {
  if (path == nullptr)
    return false;
  std::error_code ec;
  return fs::is_regular_file(to_path(path), ec);
}

bool file_read(const char *path, std::string &out) {
  if (path == nullptr)
    return false;
  std::ifstream in(to_path(path), std::ios::binary);
  if (!in)
    return false;
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  if (in.bad())
    return false;
  out = std::move(data);
  return true;
}

bool file_write(const char *path, std::string_view data) {
  if (path == nullptr)
    return false;
  const fs::path target = to_path(path);
  std::error_code ec;
  if (target.has_parent_path())
    fs::create_directories(target.parent_path(), ec);

  // Write beside the target, then rename over it: a crash mid-write leaves
  // the old file whole instead of a truncated one.
  fs::path temp = target;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      NJIN_WARN("file: cannot open for writing: %s", path);
      return false;
    }
    out.write(data.data(), (std::streamsize)data.size());
    if (!out) {
      NJIN_WARN("file: write failed: %s", path);
      return false;
    }
  }
  fs::rename(temp, target, ec);
  if (ec) {
    NJIN_WARN("file: cannot replace %s: %s", path, ec.message().c_str());
    fs::remove(temp, ec);
    return false;
  }
  return true;
}

std::string save_path(const njin_ctx &ctx, const char *file_name) {
  const char *app = ctx.cfg.app_name != nullptr ? ctx.cfg.app_name : ctx.cfg.title;
  const fs::path dir = user_data_dir() / to_path(folder_name(app).c_str());
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec)
    NJIN_WARN("file: cannot create save folder: %s", ec.message().c_str());
  fs::path full = file_name != nullptr ? dir / to_path(file_name) : dir;
  // "screenshots/a.png" on Windows would otherwise mix / and \ in one path.
  full.make_preferred();
  const auto u8 = full.u8string();
  return std::string(u8.begin(), u8.end());
}
} // namespace njin
