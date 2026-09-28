#include "panels.h"
#include "imgui.h"

namespace ui_editor {

namespace {
void edit_color(const char *label, njin::rgba &color) {
  ImGui::ColorEdit4(label, &color.r, ImGuiColorEditFlags_AlphaBar);
}

void set_roundness(njin::ui_look &look, float r) {
  look.normal.roundness = look.focused.roundness = look.pressed.roundness = look.disabled.roundness = r;
}
} // namespace

void theme_window(editor_app &app) {
  if (!app.show_theme)
    return;
  if (!ImGui::Begin("Theme###theme", &app.show_theme)) {
    ImGui::End();
    return;
  }
  ImGui::BeginDisabled(app.play_mode);
  njin::ui_style &st = app.layout.style;

  ImGui::Checkbox("Style riêng cho layout (custom_style)", &app.layout.custom_style);
  if (!app.layout.custom_style) {
    ImGui::TextWrapped("Layout kế thừa style của game (khuyên dùng): file không lưu style, game tự "
                       "gọi ui_style_set. Viewport xem trước bằng style chọn trên thanh công cụ.");
    ImGui::EndDisabled();
    ImGui::End();
    return;
  }
  ImGui::TextWrapped("Style dưới đây được lưu vào file và áp khi vẽ layout.");

  ImGui::SeparatorText("Mẫu");
  if (ImGui::Button("Mặc định"))
    st = njin::ui_default_style();
  ImGui::SameLine();
  if (ImGui::Button("Pixel"))
    st = njin::ui_pixel_style();

  ImGui::PushItemWidth(-130);
  ImGui::SeparatorText("Kích thước");
  ImGui::DragFloat("Cỡ chữ", &st.font_size, 0.25f, 6.0f, 96.0f, "%.0f px");
  ImGui::DragFloat("Scale", &st.scale, 0.01f, 0.25f, 4.0f, "%.2fx");
  ImGui::DragFloat("Padding", &st.padding, 0.25f, 0.0f, 80.0f, "%.0f px");
  ImGui::DragFloat("Spacing", &st.spacing, 0.25f, 0.0f, 60.0f, "%.0f px");
  ImGui::DragFloat("Cao widget", &st.widget_height, 0.25f, 8.0f, 160.0f, "%.0f px");
  ImGui::DragFloat("Rộng panel", &st.width, 1.0f, 60.0f, 2000.0f, "%.0f px");

  ImGui::SeparatorText("Bo góc (0 = vuông)");
  float r = st.button.normal.roundness;
  if (ImGui::SliderFloat("Nút", &r, 0.0f, 1.0f, "%.2f"))
    set_roundness(st.button, r);
  ImGui::SliderFloat("Panel", &st.panel.normal.roundness, 0.0f, 0.5f, "%.2f");
  r = st.track.normal.roundness;
  if (ImGui::SliderFloat("Rãnh / núm", &r, 0.0f, 1.0f, "%.2f")) {
    set_roundness(st.track, r);
    set_roundness(st.fill, r);
    set_roundness(st.knob, r);
  }

  ImGui::SeparatorText("Màu");
  edit_color("Nền panel", st.panel.normal.color);
  edit_color("Viền panel", st.panel.normal.outline);
  edit_color("Chữ tiêu đề", st.panel.text);
  edit_color("Nút", st.button.normal.color);
  edit_color("Nút (focus)", st.button.focused.color);
  edit_color("Nút (nhấn)", st.button.pressed.color);
  edit_color("Chữ nút", st.button.text);
  edit_color("Rãnh", st.track.normal.color);
  edit_color("Phần đầy", st.fill.normal.color);
  edit_color("Núm", st.knob.normal.color);
  ImGui::TextDisabled("File chỉ lưu những màu có ở đây (ui_layout_to_json).");
  ImGui::PopItemWidth();
  ImGui::EndDisabled();
  ImGui::End();
}

} // namespace ui_editor
