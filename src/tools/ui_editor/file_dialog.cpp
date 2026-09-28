#include "file_dialog.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <cstring>

namespace ui_editor {

std::string open_file_dialog() {
  char filename[MAX_PATH] = "";
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "Njin UI Files (*.json)\0*.json;*.ui.json\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = filename;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameA(&ofn))
    return std::string(filename);
  return "";
}

std::string save_file_dialog(const std::string &default_name) {
  char filename[MAX_PATH] = "";
  if (!default_name.empty())
    std::strncpy(filename, default_name.c_str(), sizeof filename);
  else
    std::strncpy(filename, "menu.ui.json", sizeof filename);

  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "Njin UI Files (*.json)\0*.json;*.ui.json\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = filename;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrDefExt = "json";
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (GetSaveFileNameA(&ofn))
    return std::string(filename);
  return "";
}

} // namespace ui_editor

#else

namespace ui_editor {
std::string open_file_dialog() { return ""; }
std::string save_file_dialog(const std::string &) { return ""; }
} // namespace ui_editor

#endif
