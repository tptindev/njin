// Small helpers the panels share: log colours, text search, number and JSON
// formatting.
#pragma once
#include "app.h"
#include "imgui.h"
#include <cstddef>
#include <string>

namespace inspector {
// Colour and name of a log level (0 trace .. 5 fatal).
ImVec4 level_color(int lv);
const char *level_name(int lv);

// The names of an entity's components, comma separated.
std::string comps_text(const app &a, const entity_row &r);

// Case-insensitive substring test. An empty needle matches everything.
bool contains_ci(const std::string &hay, const char *needle);

// A number as people read it: 1480, 0.25, 12.5, not 1.48e+03.
std::string fmt_num(double v);

// Bytes as people read them: 512 B, 3.4 KB, 12.5 MB, 1.20 GB.
std::string fmt_bytes(double bytes);

// Shows a JSON value as a compact read-only tree.
void show_value(const char *key, const json_value &v);
} // namespace inspector
