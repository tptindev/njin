// njin_ui_editor: builds .ui.json layouts the way Godot builds a Control scene.
// Nodes are dragged from the palette into the tree or the Viewport, the tree is
// reordered by drag and drop, and the Viewport shows the layout drawn by the
// engine's own UI code. The windows dock (Dear ImGui docking branch).
#include "app.h"
#include "file_dialog.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "njin.h"
#include "panels.h"
#include "preview.h"
#include "raylib.h"
#include "rlImGui.h"
#include <cstdio>
#include <string>

namespace ui_editor {
namespace {

void save(editor_app &app, bool as) {
  std::string path = app.file_path;
  if (as || path.empty())
    path = save_file_dialog(path.empty() ? "menu.ui.json" : path);
  if (!path.empty())
    app.save_file(path);
}

void open(editor_app &app) {
  const std::string path = open_file_dialog();
  if (!path.empty())
    app.open_file(path);
}

void toggle_play(editor_app &app) {
  if (app.play_mode)
    app.leave_play();
  else
    app.enter_play();
}

void shortcuts(editor_app &app) {
  ImGuiIO &io = ImGui::GetIO();
  using namespace ImGui;
  if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N))
    app.new_layout();
  if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
    open(app);
  if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
    save(app, false);
  if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S))
    save(app, true);
  if (IsKeyPressed(ImGuiKey_F5, false))
    toggle_play(app);
  // Text fields keep their own undo, delete and copy.
  if (io.WantTextInput || app.play_mode)
    return;
  if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
    app.undo();
  if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
    app.redo();
  // Node commands, like Godot's, while the tree or the Viewport has focus.
  if (!app.scene_focused && !app.view_focused)
    return;
  const node_ref n = app.sel;
  if (IsKeyPressed(ImGuiKey_Delete, false))
    app.defer([&app, n] { app.remove(n); });
  if (IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
    app.defer([&app, n] { app.sel = app.duplicate(n); });
}

void menu_bar(editor_app &app) {
  if (!ImGui::BeginMainMenuBar())
    return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Mới", "Ctrl+N"))
      app.new_layout();
    if (ImGui::MenuItem("Mở...", "Ctrl+O"))
      open(app);
    if (ImGui::MenuItem("Lưu", "Ctrl+S"))
      save(app, false);
    if (ImGui::MenuItem("Lưu thành...", "Ctrl+Shift+S"))
      save(app, true);
    ImGui::Separator();
    if (ImGui::MenuItem("Nạp layout mẫu"))
      app.add_default_demo();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Sửa")) {
    if (ImGui::MenuItem("Hoàn tác", "Ctrl+Z", false, !app.undo_stack.empty() && !app.play_mode))
      app.undo();
    if (ImGui::MenuItem("Làm lại", "Ctrl+Y", false, !app.redo_stack.empty() && !app.play_mode))
      app.redo();
    ImGui::Separator();
    const node_ref n = app.sel;
    const bool node = n.kind == node_ref::panel || n.kind == node_ref::widget || n.kind == node_ref::popup;
    if (ImGui::BeginMenu("Thêm node", !app.play_mode)) {
      add_node_menu_items(app, n);
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Nhân đôi", "Ctrl+D", false, node && !app.play_mode))
      app.defer([&app, n] { app.sel = app.duplicate(n); });
    if (ImGui::MenuItem("Xóa", "Del", false, node && !app.play_mode))
      app.defer([&app, n] { app.remove(n); });
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Chạy")) {
    if (ImGui::MenuItem(app.play_mode ? "Dừng" : "Chạy thử", "F5"))
      toggle_play(app);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Cửa sổ")) {
    ImGui::MenuItem("Scene", nullptr, &app.show_scene);
    ImGui::MenuItem("Nodes", nullptr, &app.show_nodes);
    ImGui::MenuItem("Viewport", nullptr, &app.show_viewport);
    ImGui::MenuItem("Inspector", nullptr, &app.show_inspector);
    ImGui::MenuItem("Theme", nullptr, &app.show_theme);
    ImGui::MenuItem("C++", nullptr, &app.show_code);
    ImGui::MenuItem("JSON", nullptr, &app.show_json);
    ImGui::MenuItem("Output", nullptr, &app.show_output);
    ImGui::Separator();
    if (ImGui::MenuItem("Bố cục mặc định"))
      app.reset_dock = true;
    ImGui::EndMenu();
  }
  if (app.play_mode) {
    ImGui::SameLine(ImGui::GetWindowWidth() - 150);
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "▶ ĐANG CHẠY THỬ");
  }
  ImGui::EndMainMenuBar();
}

void status_bar(editor_app &app) {
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
  if (ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), flags)) {
    if (ImGui::BeginMenuBar()) {
      const std::string name = app.file_path.empty() ? "(chưa lưu)" : app.file_path;
      ImGui::Text("%s%s", name.c_str(), app.dirty() ? " *" : "");
      ImGui::SameLine(0, 24);
      ImGui::TextDisabled("%s", app.status_text.c_str());
      ImGui::EndMenuBar();
    }
  }
  ImGui::End();
}

// Godot's default arrangement: Scene over Nodes on the left, the Viewport in
// the middle, Inspector and Theme on the right, the code and log below.
void build_dock(ImGuiID id) {
  ImGui::DockBuilderRemoveNode(id);
  ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(id, ImGui::GetMainViewport()->WorkSize);
  ImGuiID center = id;
  ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
  ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
  ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.27f, nullptr, &center);
  ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.42f, nullptr, &left);
  ImGui::DockBuilderDockWindow("Scene###scene", left);
  ImGui::DockBuilderDockWindow("Nodes###nodes", left_bottom);
  ImGui::DockBuilderDockWindow("Viewport###viewport", center);
  ImGui::DockBuilderDockWindow("Inspector###inspector", right);
  ImGui::DockBuilderDockWindow("Theme###theme", right);
  ImGui::DockBuilderDockWindow("Output###output", bottom);
  ImGui::DockBuilderDockWindow("C++###code", bottom);
  ImGui::DockBuilderDockWindow("JSON###json", bottom);
  ImGui::DockBuilderFinish(id);
}

void dockspace(editor_app &app) {
  const ImGuiID id = ImHashStr("njin_ui_editor_dock");
  if (app.reset_dock || ImGui::DockBuilderGetNode(id) == nullptr) {
    build_dock(id);
    app.show_scene = app.show_nodes = app.show_viewport = app.show_inspector = true;
    app.show_theme = app.show_code = app.show_json = app.show_output = true;
    app.reset_dock = false;
  }
  ImGui::DockSpaceOverViewport(id, ImGui::GetMainViewport());
}

void theme() {
  ImGui::StyleColorsDark();
  ImGuiStyle &st = ImGui::GetStyle();
  st.WindowRounding = 4.0f;
  st.FrameRounding = 3.0f;
  st.PopupRounding = 4.0f;
  st.TabRounding = 3.0f;
  st.GrabRounding = 3.0f;
  st.WindowBorderSize = 0.0f;
  ImVec4 *c = st.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.17f, 1.0f);
  c[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.12f, 0.15f, 1.0f);
  c[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.22f, 0.30f, 0.42f, 1.0f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.36f, 0.52f, 1.0f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.46f, 0.78f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.28f, 1.0f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.32f, 0.42f, 1.0f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.46f, 0.78f, 1.0f);
  c[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.19f, 0.24f, 1.0f);
  c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
  c[ImGuiCol_Tab] = ImVec4(0.15f, 0.16f, 0.20f, 1.0f);
  c[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.25f, 0.32f, 1.0f);
  c[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.34f, 0.46f, 1.0f);
  c[ImGuiCol_DockingPreview] = ImVec4(0.35f, 0.62f, 1.0f, 0.6f);
}

void load_font() {
  ImGuiIO &io = ImGui::GetIO();
  const char *fonts[] = {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                         "/System/Library/Fonts/Supplemental/Arial.ttf"};
  for (const char *path : fonts) {
    if (!FileExists(path))
      continue;
    if (ImFont *font = io.Fonts->AddFontFromFileTTF(path, 16.0f)) {
      io.FontDefault = font;
      // Symbols (▶ ■ ×) that Segoe UI lacks.
      if (FileExists("C:/Windows/Fonts/seguisym.ttf")) {
        ImFontConfig merge;
        merge.MergeMode = true;
        io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisym.ttf", 16.0f, &merge);
      }
      return;
    }
  }
}

} // namespace
} // namespace ui_editor

int main(int argc, char **argv) {
  using namespace ui_editor;
  njin::config cfg{.title = "njin UI Editor", .width = 1600.0f, .height = 920.0f, .target_fps = 60.0f};
  cfg.exit_key = njin::key_none;
  cfg.resizable = true;
  cfg.vsync = true;
  njin::context *ctx = njin::create(cfg);

  editor_app app;
  app.ctx = ctx;
  app.add_default_demo();
  if (argc > 1)
    app.open_file(argv[1]);

  rlImGuiBeginInitImGui();
  theme();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  load_font();
  rlImGuiEndInitImGui();
  io.IniFilename = "njin_ui_editor.ini";

  std::string title;
  while (!WindowShouldClose()) {
    preview_begin_frame(app);
    BeginDrawing();
    ClearBackground(Color{20, 21, 26, 255});
    preview_render(app);

    rlImGuiBegin();
    shortcuts(app);
    menu_bar(app);
    status_bar(app);
    dockspace(app);
    scene_window(app);
    nodes_window(app);
    viewport_window(app);
    inspector_window(app);
    theme_window(app);
    codegen_window(app);
    json_window(app);
    output_window(app);
    // A drag or a text edit in progress is one undo step, taken when it ends.
    app.end_frame(ImGui::IsAnyItemActive() || ImGui::GetDragDropPayload() != nullptr);
    rlImGuiEnd();
    EndDrawing();

    const std::string want = std::string("njin UI Editor - ") +
                             (app.file_path.empty() ? "(chưa lưu)" : app.file_path) + (app.dirty() ? " *" : "");
    if (want != title) {
      title = want;
      SetWindowTitle(title.c_str());
    }
  }

  preview_shutdown();
  rlImGuiShutdown();
  njin::destroy(ctx);
  return 0;
}
