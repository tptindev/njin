// "Systems": where the frame's CPU time goes, per phase and per system.
#include "imgui.h"
#include "implot.h"
#include "panels.h"
#include "util.h"
#include <algorithm>
#include <cstdio>
#include <numeric>

namespace inspector {
namespace {
// A stacked bar of the phases against the frame time, so what is left over
// (waiting for vsync, presenting, the OS) is visible too.
void frame_budget(const app &a) {
  float sum = 0;
  for (const phase_row &p : a.phases)
    sum += p.ms;
  const float frame = std::max(a.prof_frame, sum);
  const float other = std::max(0.0f, a.prof_frame - sum);
  ImGui::Text("frame %.2f ms  (%.0f fps)   systems %.2f ms   waiting / other %.2f ms", a.prof_frame,
              a.prof_frame > 0 ? 1000.0f / a.prof_frame : 0.0f, sum, other);
  if (!ImPlot::BeginPlot("##budget", {-1, 74}, ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText | ImPlotFlags_NoLegend))
    return;
  ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoGridLines, ImPlotAxisFlags_NoDecorations);
  ImPlot::SetupAxisLimits(ImAxis_X1, 0, std::max(frame, 1.0f), ImPlotCond_Always);
  ImPlot::SetupAxisLimits(ImAxis_Y1, -0.6, 0.6, ImPlotCond_Always);
  ImPlot::SetupLegend(ImPlotLocation_South, ImPlotLegendFlags_Horizontal);
  static const ImVec4 colors[] = {{0.55f, 0.55f, 0.6f, 1}, {0.35f, 0.65f, 1, 1},  {0.45f, 0.85f, 0.5f, 1},
                                  {0.95f, 0.8f, 0.3f, 1},  {0.9f, 0.5f, 0.35f, 1}, {0.75f, 0.5f, 0.95f, 1},
                                  {0.4f, 0.85f, 0.85f, 1}, {0.95f, 0.55f, 0.75f, 1}, {0.6f, 0.6f, 0.6f, 1}};
  double left = 0;
  ImDrawList *dl = ImPlot::GetPlotDrawList();
  const auto block = [&](const char *name, double ms, ImVec4 color) {
    const ImVec2 a0 = ImPlot::PlotToPixels(left, 0.4), a1 = ImPlot::PlotToPixels(left + ms, -0.4);
    dl->AddRectFilled(a0, a1, ImGui::ColorConvertFloat4ToU32(color));
    dl->AddRect(a0, a1, IM_COL32(20, 22, 28, 255));
    // A legend entry with the same colour.
    ImPlot::PlotDummy(name, ImPlotSpec(ImPlotProp_LineColor, color, ImPlotProp_FillColor, color));
    left += ms;
  };
  for (size_t i = 0; i < a.phases.size(); i++)
    if (a.phases[i].ms > 0.001f)
      block(a.phases[i].name.c_str(), a.phases[i].ms, colors[i % 9]);
  if (other > 0.001f)
    block("wait / other", other, {0.25f, 0.27f, 0.32f, 1});
  ImPlot::EndPlot();
}
} // namespace

void systems_window(app &a) {
  if (!begin_panel(a, "Systems"))
    return;
  if (a.systems.empty()) {
    ImGui::TextDisabled("Waiting for timings from the game...");
    ImGui::End();
    return;
  }
  frame_budget(a);

  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##sysfilter", "filter: module or system", a.sys_filter, sizeof a.sys_filter);
  ImGui::SameLine();
  ImGui::Checkbox("hide idle", &a.sys_hide_idle);
  ImGui::SameLine();
  ImGui::TextDisabled("ms are the last frame; avg is smoothed; peak decays slowly");

  const float budget = std::max(a.prof_frame, 1.0f);
  if (ImGui::BeginTable("systems", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("system", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 3);
    ImGui::TableSetupColumn("phase", ImGuiTableColumnFlags_WidthFixed, 84);
    ImGui::TableSetupColumn("avg ms", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort |
                                          ImGuiTableColumnFlags_PreferSortDescending, 62);
    ImGui::TableSetupColumn("last ms", ImGuiTableColumnFlags_WidthFixed, 62);
    ImGui::TableSetupColumn("peak ms", ImGuiTableColumnFlags_WidthFixed, 62);
    ImGui::TableSetupColumn("calls", ImGuiTableColumnFlags_WidthFixed, 44);
    ImGui::TableHeadersRow();

    std::vector<const system_row *> rows;
    for (const system_row &s : a.systems) {
      if (a.sys_hide_idle && s.avg < 0.005f && s.peak < 0.02f)
        continue;
      if (!contains_ci(s.name, a.sys_filter))
        continue;
      rows.push_back(&s);
    }
    int column = 2;
    bool ascending = false;
    if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
      column = specs->Specs[0].ColumnIndex;
      ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const system_row *x, const system_row *y) {
      float vx = x->avg, vy = y->avg;
      if (column == 1) { vx = (float)x->phase; vy = (float)y->phase; }
      else if (column == 3) { vx = x->ms; vy = y->ms; }
      else if (column == 4) { vx = x->peak; vy = y->peak; }
      else if (column == 5) { vx = (float)x->calls; vy = (float)y->calls; }
      return ascending ? vx < vy : vx > vy;
    });

    for (const system_row *s : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      // The bar behind the name is the system's share of the frame.
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = ImGui::GetContentRegionAvail().x;
      ImGui::GetWindowDrawList()->AddRectFilled(
          p, {p.x + w * std::clamp(s->avg / budget, 0.0f, 1.0f), p.y + ImGui::GetTextLineHeight()},
          IM_COL32(60, 110, 200, 90));
      ImGui::TextUnformatted(s->name.c_str());
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", a.phases.size() > (size_t)s->phase ? a.phases[(size_t)s->phase].name.c_str() : "?");
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", s->avg);
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", s->ms);
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", s->peak);
      ImGui::TableNextColumn();
      ImGui::Text("%d", s->calls);
    }
    ImGui::EndTable();
  }
  ImGui::End();
}
} // namespace inspector
