// "Assets": every resource the game has loaded and what it costs, on the GPU
// (textures, targets, font atlases) and in RAM (decoded sounds, tables).
#include "imgui.h"
#include "implot.h"
#include "panels.h"
#include "util.h"
#include <algorithm>
#include <map>

namespace inspector {
void assets_window(app &a) {
  if (!begin_panel(a, "Assets"))
    return;
  if (a.resources.empty()) {
    ImGui::TextDisabled("Waiting for the game...");
    ImGui::End();
    return;
  }
  // The game's own count against what the OS reports for the process.
  const proc_sample &s = a.mon.last;
  ImGui::Text("GPU  %s known to the game", fmt_bytes((double)a.res_gpu).c_str());
  if (s.gpu_valid) {
    ImGui::SameLine();
    ImGui::TextDisabled("(the OS counts %s dedicated + %s shared memory for the process, driver buffers "
                        "and the swap chain included)",
                        fmt_bytes(s.gpu_dedicated).c_str(), fmt_bytes(s.gpu_shared).c_str());
  }
  ImGui::Text("RAM  %s known to the game (decoded audio, engine tables)", fmt_bytes((double)a.res_ram).c_str());

  // Totals by kind.
  std::map<std::string, long long> by_kind;
  for (const res_row &r : a.resources)
    by_kind[r.kind] += r.bytes;
  ImGui::TextDisabled("by kind:");
  for (const auto &[kind, bytes] : by_kind) {
    ImGui::SameLine();
    ImGui::Text("%s %s", kind.c_str(), fmt_bytes((double)bytes).c_str());
  }

  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##resfilter", "filter: kind or name", a.res_filter, sizeof a.res_filter);
  if (ImGui::BeginTable("assets", 5,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, 64);
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 3);
    ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 2);
    ImGui::TableSetupColumn("memory", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort |
                                          ImGuiTableColumnFlags_PreferSortDescending, 76);
    ImGui::TableSetupColumn("where", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 48);
    ImGui::TableHeadersRow();

    std::vector<const res_row *> rows;
    for (const res_row &r : a.resources)
      if (contains_ci(r.kind, a.res_filter) || contains_ci(r.name, a.res_filter))
        rows.push_back(&r);
    int column = 3;
    bool ascending = false;
    if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
      column = specs->Specs[0].ColumnIndex;
      ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const res_row *x, const res_row *y) {
      if (column == 0)
        return ascending ? x->kind < y->kind : x->kind > y->kind;
      return ascending ? x->bytes < y->bytes : x->bytes > y->bytes;
    });
    const double total = (double)std::max(1LL, a.res_gpu + a.res_ram);
    for (const res_row *r : rows) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r->kind.c_str());
      ImGui::TableNextColumn();
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = ImGui::GetContentRegionAvail().x;
      ImGui::GetWindowDrawList()->AddRectFilled(
          p, {p.x + w * (float)std::min(1.0, (double)r->bytes / total), p.y + ImGui::GetTextLineHeight()},
          r->gpu ? IM_COL32(230, 150, 60, 90) : IM_COL32(90, 200, 120, 90));
      ImGui::TextUnformatted(r->name.c_str());
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", r->info.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%s", r->bytes > 0 ? fmt_bytes((double)r->bytes).c_str() : "-");
      ImGui::TableNextColumn();
      ImGui::TextColored(r->gpu ? ImVec4{0.95f, 0.65f, 0.3f, 1} : ImVec4{0.45f, 0.85f, 0.5f, 1}, "%s",
                         r->gpu ? "GPU" : "RAM");
    }
    ImGui::EndTable();
  }
  ImGui::End();
}
} // namespace inspector
