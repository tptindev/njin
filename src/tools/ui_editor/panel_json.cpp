#include "panels.h"
#include "imgui.h"

namespace ui_editor {

void json_window(editor_app &app) {
  if (!app.show_json)
    return;
  if (!ImGui::Begin("JSON###json", &app.show_json)) {
    ImGui::End();
    return;
  }
  // The committed text is what Save writes; it lags a drag in progress by design.
  const std::string &text = app.committed;
  if (ImGui::Button("Sao chép"))
    ImGui::SetClipboardText(text.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("Nội dung file .ui.json (nạp bằng njin::ui_layout_load)");
  ImGui::InputTextMultiline("##json", const_cast<char *>(text.c_str()), text.size() + 1, ImVec2(-1, -1),
                            ImGuiInputTextFlags_ReadOnly);
  ImGui::End();
}

void output_window(editor_app &app) {
  if (!app.show_output)
    return;
  if (!ImGui::Begin("Output###output", &app.show_output)) {
    ImGui::End();
    return;
  }
  if (ImGui::SmallButton("Xóa"))
    app.output.clear();
  ImGui::SameLine();
  ImGui::TextDisabled("Sự kiện ui_layout_event khi chạy thử");
  ImGui::Separator();
  ImGui::BeginChild("log");
  for (const auto &line : app.output)
    ImGui::TextUnformatted(line.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
    ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

} // namespace ui_editor
