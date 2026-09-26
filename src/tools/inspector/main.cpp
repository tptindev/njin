// njin_inspector: connects to a running njin game (njin::debug_server_start)
// over 127.0.0.1 and shows its state with Dear ImGui, in its own window.
//
//   njin_inspector [--port 7779]
//
// It retries until a game is listening, and again whenever the game restarts.
#include "app.h"
#include "imgui.h"
#include "implot.h"
#include "panels.h"
#include "raylib.h"
#include "rlImGui.h"
#include <cstdlib>
#include <cstring>

namespace {
// A system font with Vietnamese glyphs, so level names and log lines in the
// game's language read correctly. Falls back to ImGui's built-in font.
void load_font() {
  const char *candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                              "/System/Library/Fonts/Supplemental/Arial.ttf"};
  for (const char *path : candidates) {
    if (FileExists(path)) {
      ImGuiIO &io = ImGui::GetIO();
      // rlImGui has already added ImGui's own font, which stays the default
      // unless told otherwise.
      if (ImFont *font = io.Fonts->AddFontFromFileTTF(path, 17.0f))
        io.FontDefault = font;
      return;
    }
  }
}
} // namespace

int main(int argc, char **argv) {
  inspector::app a;
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], "--port") == 0)
      a.port = std::atoi(argv[i + 1]);
  }
  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
  InitWindow(1700, 960, "njin inspector");
  SetExitKey(KEY_NULL);
  SetTargetFPS(60);
  rlImGuiBeginInitImGui();
  ImGui::StyleColorsDark();
  load_font();
  rlImGuiEndInitImGui();
  ImPlot::CreateContext();
  ImGui::GetIO().IniFilename = "njin_inspector.ini"; // remembers the layout

  while (!WindowShouldClose()) {
    inspector::update_connection(a, GetFrameTime());
    BeginDrawing();
    ClearBackground(Color{14, 15, 19, 255});
    rlImGuiBegin();
    inspector::status_bar(a);
    inspector::performance_window(a);
    inspector::entities_window(a);
    inspector::world_window(a);
    inspector::inspector_window(a);
    inspector::watches_window(a);
    inspector::log_window(a);
    inspector::monitor_window(a);
    inspector::systems_window(a);
    inspector::memory_window(a);
    inspector::assets_window(a);
    inspector::end_frame_layout(a);
    rlImGuiEnd();
    EndDrawing();
  }
  a.link.close();
  njin::net_close(a.connecting);
  ImPlot::DestroyContext();
  rlImGuiShutdown();
  CloseWindow();
  return 0;
}
