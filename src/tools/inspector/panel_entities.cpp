#include "panels.h"
#include "util.h"
#include "imgui.h"
#include <string>

namespace inspector {
void entities_window(app &a) {
  if (!begin_panel(a, "Entities"))
    return;
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##filter", "filter: name, id or component", a.entity_filter, sizeof a.entity_filter);
  ImGui::Text("%zu shown", a.ents.size());
  if (a.truncated) {
    ImGui::SameLine();
    ImGui::TextColored({1, 0.7f, 0.3f, 1}, "(list truncated: raise debug_server_desc::max_entities)");
  }
  if (ImGui::BeginTable("ents", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, 60);
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 120);
    ImGui::TableSetupColumn("RAM", ImGuiTableColumnFlags_WidthFixed, 64);
    ImGui::TableSetupColumn("GPU", ImGuiTableColumnFlags_WidthFixed, 64);
    ImGui::TableSetupColumn("components");
    ImGui::TableHeadersRow();
    for (const entity_row &r : a.ents) {
      const std::string comps = comps_text(a, r);
      const std::string id = std::to_string(r.id & 0xFFFFFu);
      if (!contains_ci(r.label, a.entity_filter) && !contains_ci(comps, a.entity_filter) &&
          !contains_ci(id, a.entity_filter))
        continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID((int)r.id);
      if (ImGui::Selectable(id.c_str(), a.selected == (long long)r.id, ImGuiSelectableFlags_SpanAllColumns))
        select(a, r.id);
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.label.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.ram > 0 ? fmt_bytes((double)r.ram).c_str() : "-");
      ImGui::TableNextColumn();
      if (r.gpu_mem > 0)
        ImGui::TextUnformatted(fmt_bytes((double)r.gpu_mem).c_str());
      else
        ImGui::TextDisabled("-");
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", comps.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::End();
}
} // namespace inspector
