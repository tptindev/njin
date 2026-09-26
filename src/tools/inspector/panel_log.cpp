#include "panels.h"
#include "util.h"
#include "imgui.h"

namespace inspector {
void log_window(app &a) {
  if (!begin_panel(a, "Log"))
    return;
  static const char *levels[] = {"trace", "debug", "info", "warn", "error", "fatal"};
  ImGui::SetNextItemWidth(90);
  ImGui::Combo("##lv", &a.log_min_level, levels, 6);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##lf", "filter", a.log_filter, sizeof a.log_filter);
  ImGui::SameLine();
  if (ImGui::Button("Clear"))
    a.logs.clear();
  ImGui::SameLine();
  ImGui::Checkbox("follow", &a.log_autoscroll);
  ImGui::BeginChild("lines", {0, 0}, ImGuiChildFlags_Borders);
  for (const log_row &l : a.logs) {
    if (l.level < a.log_min_level)
      continue;
    if (!contains_ci(l.msg, a.log_filter) && !contains_ci(l.src, a.log_filter))
      continue;
    ImGui::TextColored(level_color(l.level), "%-5s", level_name(l.level));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", l.src.c_str());
    ImGui::SameLine();
    ImGui::TextColored(level_color(l.level), "%s", l.msg.c_str());
  }
  if (a.log_autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40)
    ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}
} // namespace inspector
