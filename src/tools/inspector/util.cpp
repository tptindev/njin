#include "util.h"
#include "panels.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace inspector {
namespace {
struct slot {
  const char *name;
  bool overview;
  ImVec2 pos_overview, size_overview;
  bool consumption;
  ImVec2 pos_consumption, size_consumption;
};
// Where each window lives in each layout.
const slot slots[] = {
    {"Performance", true, {8, 30}, {420, 300}, true, {8, 522}, {540, 420}},
    {"Entities", true, {8, 338}, {420, 520}, true, {1124, 522}, {568, 420}},
    {"World", true, {436, 30}, {588, 576}, false, {0, 0}, {0, 0}},
    {"Inspector", true, {1032, 30}, {360, 520}, false, {0, 0}, {0, 0}},
    {"Watches", true, {1032, 558}, {360, 300}, false, {0, 0}, {0, 0}},
    {"Log", true, {436, 614}, {588, 244}, false, {0, 0}, {0, 0}},
    {"Process", false, {0, 0}, {0, 0}, true, {8, 30}, {540, 484}},
    {"Systems", false, {0, 0}, {0, 0}, true, {556, 30}, {560, 484}},
    {"Memory", false, {0, 0}, {0, 0}, true, {1124, 30}, {568, 484}},
    {"Assets", false, {0, 0}, {0, 0}, true, {556, 522}, {560, 420}},
};
} // namespace

bool begin_panel(app &a, const char *name) {
  for (const slot &s : slots) {
    if (std::strcmp(s.name, name) != 0)
      continue;
    const bool shown = a.layout == 0 ? s.overview : s.consumption;
    if (!shown)
      return false;
    if (a.layout_dirty) {
      ImGui::SetNextWindowPos(a.layout == 0 ? s.pos_overview : s.pos_consumption, ImGuiCond_Always);
      ImGui::SetNextWindowSize(a.layout == 0 ? s.size_overview : s.size_consumption, ImGuiCond_Always);
    }
    if (!ImGui::Begin(name)) {
      ImGui::End();
      return false;
    }
    return true;
  }
  return ImGui::Begin(name);
}

void end_frame_layout(app &a) { a.layout_dirty = false; }
ImVec4 level_color(int lv) {
  switch (lv) {
  case 0: return {0.55f, 0.55f, 0.60f, 1};
  case 1: return {0.65f, 0.75f, 0.85f, 1};
  case 3: return {0.98f, 0.78f, 0.30f, 1};
  case 4:
  case 5: return {1.00f, 0.42f, 0.42f, 1};
  default: return {0.90f, 0.92f, 0.96f, 1};
  }
}

const char *level_name(int lv) {
  static const char *names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
  return lv >= 0 && lv <= 5 ? names[lv] : "?";
}

std::string comps_text(const app &a, const entity_row &r) {
  std::string s;
  for (int c : r.comps) {
    if (!s.empty())
      s += ", ";
    s += c >= 0 && c < (int)a.types.size() ? a.types[(size_t)c] : "?";
  }
  return s;
}

bool contains_ci(const std::string &hay, const char *needle) {
  if (*needle == '\0')
    return true;
  const size_t n = std::strlen(needle);
  for (size_t i = 0; i + n <= hay.size(); i++) {
    size_t k = 0;
    while (k < n && std::tolower((unsigned char)hay[i + k]) == std::tolower((unsigned char)needle[k]))
      k++;
    if (k == n)
      return true;
  }
  return false;
}

// A number as people read it: 1480, 0.25, 12.5, not 1.48e+03.
std::string fmt_num(double v) {
  char buf[48];
  if (v == std::floor(v) && std::fabs(v) < 1e12)
    std::snprintf(buf, sizeof buf, "%.0f", v);
  else {
    std::snprintf(buf, sizeof buf, "%.3f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0')
      s.pop_back();
    if (!s.empty() && s.back() == '.')
      s.pop_back();
    return s;
  }
  return buf;
}

std::string fmt_bytes(double bytes) {
  static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int u = 0;
  double v = bytes < 0 ? 0 : bytes;
  while (v >= 1024.0 && u < 4) {
    v /= 1024.0;
    u++;
  }
  char buf[48];
  if (u == 0)
    std::snprintf(buf, sizeof buf, "%.0f %s", v, units[u]);
  else
    std::snprintf(buf, sizeof buf, v >= 100 ? "%.0f %s" : (v >= 10 ? "%.1f %s" : "%.2f %s"), v, units[u]);
  return buf;
}

// Shows a JSON value as a compact read-only tree.
void show_value(const char *key, const json_value &v) {
  switch (v.kind) {
  case json_value::object:
    if (ImGui::TreeNodeEx(key, ImGuiTreeNodeFlags_DefaultOpen)) {
      for (const auto &[k, child] : v.members)
        show_value(k.c_str(), child);
      ImGui::TreePop();
    }
    return;
  case json_value::array: {
    bool numbers = v.size() <= 4;
    for (const json_value &x : v.items)
      numbers = numbers && x.is(json_value::number);
    if (numbers) {
      std::string s;
      for (const json_value &x : v.items)
        s += (s.empty() ? "" : ", ") + fmt_num(x.num);
      ImGui::Text("%s: [%s]", key, s.c_str());
      return;
    }
    if (ImGui::TreeNode(key, "%s [%zu]", key, v.size())) {
      for (size_t i = 0; i < v.items.size(); i++)
        show_value(std::to_string(i).c_str(), v.items[i]);
      ImGui::TreePop();
    }
    return;
  }
  case json_value::string: ImGui::Text("%s: \"%s\"", key, v.str.c_str()); return;
  case json_value::number: ImGui::Text("%s: %s", key, fmt_num(v.num).c_str()); return;
  case json_value::boolean: ImGui::Text("%s: %s", key, v.b ? "true" : "false"); return;
  default: ImGui::TextDisabled("%s: null", key); return;
  }
}
} // namespace inspector
