#include "panels.h"
#include "imgui.h"

namespace ui_editor {

namespace {
void code_box(editor_app &app, const char *id, const std::string &code) {
  if (ImGui::Button("Sao chép")) {
    ImGui::SetClipboardText(code.c_str());
    app.set_status("Đã sao chép code vào clipboard");
  }
  ImGui::InputTextMultiline(id, const_cast<char *>(code.c_str()), code.size() + 1, ImVec2(-1, -1),
                            ImGuiInputTextFlags_ReadOnly);
}
} // namespace

void codegen_window(editor_app &app) {
  if (!app.show_code)
    return;
  if (!ImGui::Begin("C++###code", &app.show_code)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginTabBar("code_tabs")) {
    if (ImGui::BeginTabItem("Nạp JSON (khuyên dùng)")) {
      const std::string code =
          "// Trong state của game:\n"
          "njin::ui_layout menu_ui;\n\n"
          "// Lúc khởi tạo:\n"
          "njin::ui_layout_load(ctx, \"assets/ui/menu.ui.json\", menu_ui);\n\n"
          "// Trong phase_post_render:\n"
          "njin::ui_draw_layout(ctx, menu_ui, [&](const njin::ui_layout_event &ev) {\n"
          "  // widget_id là const char*: so sánh bằng string_view, không bằng ==\n"
          "  const std::string_view id = ev.widget_id;\n"
          "  if (ev.kind == njin::ui_layout_event::button_clicked && id == \"btn_play\") {\n"
          "    // ...\n"
          "  }\n"
          "});\n\n"
          "// Hoặc đọc giá trị bất kỳ lúc nào:\n"
          "const float volume = njin::ui_layout_get_float(menu_ui, \"sld_volume\");\n";
      code_box(app, "##runtime", code);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Code immediate-mode")) {
      const ui_panel_data *p = app.current_panel();
      const std::string code = p != nullptr ? njin::ui_panel_generate_cpp(*p, ("draw_" + p->id).c_str())
                                            : njin::ui_layout_generate_cpp(app.layout);
      ImGui::TextDisabled("Cho: %s", p != nullptr ? p->id.c_str() : "toàn bộ layout");
      ImGui::SameLine();
      code_box(app, "##cpp", code);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

} // namespace ui_editor
