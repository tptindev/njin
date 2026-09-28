#pragma once
#include <string>

namespace ui_editor {

std::string open_file_dialog();
std::string save_file_dialog(const std::string &default_name);

} // namespace ui_editor
