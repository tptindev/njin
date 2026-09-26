// "Memory": how much RAM the game's data takes, by component type and by
// entity. These are the sizes the game reports for its own components, not the
// allocator's view: the Process window shows what the OS sees.
#include "imgui.h"
#include "implot.h"
#include "panels.h"
#include "util.h"
#include <algorithm>
#include <cstdio>

namespace inspector {
namespace {
void components_tab(app &a) {
  long long total = 0;
  for (const mem_type_row &r : a.mem_types)
    total += r.bytes;
  ImGui::Text("components %s   registry ids %s   %lld entities", fmt_bytes((double)a.mem_components).c_str(),
              fmt_bytes((double)a.mem_registry).c_str(), a.mem_entities);
  ImGui::TextDisabled("size: sizeof the component. heap: what it owns (vectors, strings). 8 bytes per component "
                      "are EnTT's own index. A type marked ? has no size: register it with njin::debug_component.");

  // Share of the biggest types.
  std::vector<const mem_type_row *> rows;
  for (const mem_type_row &r : a.mem_types)
    rows.push_back(&r);
  std::sort(rows.begin(), rows.end(), [](const mem_type_row *x, const mem_type_row *y) { return x->bytes > y->bytes; });
  if (total > 0 && ImPlot::BeginPlot("##pie", {170, 170}, ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText | ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
    ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);
    ImPlot::SetupAxesLimits(0, 1, 0, 1, ImPlotCond_Always);
    std::vector<const char *> labels;
    std::vector<double> values;
    double other = 0;
    for (size_t i = 0; i < rows.size(); i++) {
      if (i < 7 && rows[i]->bytes > 0) {
        labels.push_back(rows[i]->name.c_str());
        values.push_back((double)rows[i]->bytes);
      } else {
        other += (double)rows[i]->bytes;
      }
    }
    if (other > 0) {
      labels.push_back("others");
      values.push_back(other);
    }
    ImPlot::PlotPieChart(labels.data(), values.data(), (int)values.size(), 0.5, 0.5, 0.45, "", 90,
                         ImPlotSpec(ImPlotProp_Flags, ImPlotPieChartFlags_IgnoreHidden));
    ImPlot::EndPlot();
  }
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##memfilter", "filter: component", a.mem_filter, sizeof a.mem_filter);
  ImGui::EndGroup();

  if (ImGui::BeginTable("mem_types", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("component", ImGuiTableColumnFlags_WidthStretch, 3);
    ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 56);
    ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 64);
    ImGui::TableSetupColumn("heap", ImGuiTableColumnFlags_WidthFixed, 72);
    ImGui::TableSetupColumn("total", ImGuiTableColumnFlags_WidthFixed, 72);
    ImGui::TableSetupColumn("share", ImGuiTableColumnFlags_WidthFixed, 96);
    ImGui::TableHeadersRow();
    for (const mem_type_row *r : rows) {
      if (!contains_ci(r->name, a.mem_filter))
        continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r->name.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%lld", r->count);
      ImGui::TableNextColumn();
      if (r->known)
        ImGui::Text("%lld B", r->size);
      else
        ImGui::TextDisabled("?");
      ImGui::TableNextColumn();
      ImGui::Text("%s", r->heap > 0 ? fmt_bytes((double)r->heap).c_str() : "-");
      ImGui::TableNextColumn();
      ImGui::Text("%s", fmt_bytes((double)r->bytes).c_str());
      ImGui::TableNextColumn();
      ImGui::ProgressBar(total > 0 ? (float)r->bytes / (float)total : 0.0f, {-1, 12}, "");
    }
    ImGui::EndTable();
  }
}

void entities_tab(app &a) {
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##entfilter", "filter: name or component", a.entity_filter, sizeof a.entity_filter);
  ImGui::SameLine();
  ImGui::TextDisabled("what each entity holds: components, their heap, and GPU images it owns");

  std::vector<const entity_row *> rows;
  for (const entity_row &r : a.ents)
    if (contains_ci(r.label, a.entity_filter) || contains_ci(comps_text(a, r), a.entity_filter))
      rows.push_back(&r);
  if (ImGui::BeginTable("mem_ents", 5,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, 56);
    ImGui::TableSetupColumn("entity", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 2);
    ImGui::TableSetupColumn("RAM", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort |
                                       ImGuiTableColumnFlags_PreferSortDescending, 76);
    ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 76);
    ImGui::TableSetupColumn("components", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 3);
    ImGui::TableHeadersRow();
    int column = 2;
    bool ascending = false;
    if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
      column = specs->Specs[0].ColumnIndex;
      ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const entity_row *x, const entity_row *y) {
      long long vx = x->ram, vy = y->ram;
      if (column == 0) { vx = x->id; vy = y->id; }
      else if (column == 3) { vx = x->gpu_mem; vy = y->gpu_mem; }
      return ascending ? vx < vy : vx > vy;
    });
    for (const entity_row *r : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID((int)r->id);
      const std::string id = std::to_string(r->id & 0xFFFFFu);
      if (ImGui::Selectable(id.c_str(), a.selected == (long long)r->id, ImGuiSelectableFlags_SpanAllColumns))
        select(a, r->id);
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r->label.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%s", fmt_bytes((double)r->ram).c_str());
      ImGui::TableNextColumn();
      if (r->gpu_mem > 0)
        ImGui::Text("%s", fmt_bytes((double)r->gpu_mem).c_str());
      else
        ImGui::TextDisabled("-");
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", comps_text(a, *r).c_str());
    }
    ImGui::EndTable();
  }
}
} // namespace

void memory_window(app &a) {
  if (!begin_panel(a, "Memory"))
    return;
  if (a.mem_types.empty()) {
    ImGui::TextDisabled("Waiting for the game...");
    ImGui::End();
    return;
  }
  if (ImGui::BeginTabBar("memtabs")) {
    if (ImGui::BeginTabItem("Components")) {
      components_tab(a);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Entities")) {
      entities_tab(a);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}
} // namespace inspector
