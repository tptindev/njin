// njin_inspector: connects to a running njin game (njin::debug_server_start)
// over 127.0.0.1 and shows its state with Dear ImGui, in its own window.
//
//   njin_inspector [--port 7779] [--layout overview|consumption|documents] [--size 1280x720]
//
// It retries until a game is listening, and again whenever the game restarts.
// The window is a Njin context on SDL3 + bgfx; ImGui draws through
// src/tools/common/njin_imgui_host.
#include "app.h"
#include "imgui.h"
#include "njin.h"
#include "njin_imgui_host.h"
#include "panels.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
inspector::app *g_app = nullptr;

// A system font with Vietnamese glyphs, so level names and log lines in the
// game's language read correctly. Falls back to ImGui's built-in font.
void load_font() {
  const char *candidates[] = {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf",
                              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                              "/System/Library/Fonts/Supplemental/Arial.ttf"};
  for (const char *path : candidates) {
    if (njin::file_exists(path)) {
      ImGuiIO &io = ImGui::GetIO();
      if (ImFont *font = io.Fonts->AddFontFromFileTTF(path, 17.0f))
        io.FontDefault = font;
      return;
    }
  }
}

void ui(njin::context &ctx) {
  inspector::app &a = *g_app;
  inspector::update_connection(a, njin::delta_real(ctx));
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
  inspector::docs_window(a);
  inspector::end_frame_layout(a);
}
} // namespace

int main(int argc, char **argv) {
  inspector::app a;
  g_app = &a;
  int want_w = 0, want_h = 0;
  for (int i = 1; i + 1 < argc; i++) {
    if (std::strcmp(argv[i], "--port") == 0)
      a.port = std::atoi(argv[i + 1]);
    if (std::strcmp(argv[i], "--size") == 0)
      std::sscanf(argv[i + 1], "%dx%d", &want_w, &want_h);
    if (std::strcmp(argv[i], "--layout") == 0)
      a.layout = std::strcmp(argv[i + 1], "consumption") == 0 ? 1 : std::strcmp(argv[i + 1], "documents") == 0 ? 2 : 0;
  }
  njin::config cfg{.title = "njin inspector", .width = 1700.0f, .height = 960.0f, .target_fps = 60.0f};
  cfg.clear_bg_color = njin::rgba{14 / 255.0f, 15 / 255.0f, 19 / 255.0f, 1.0f};
  cfg.exit_key = njin::key_none;
  cfg.resizable = true;
  cfg.vsync = true;
  cfg.audio = false;
  njin::context *ctx = njin::create(cfg);
  {
    // A small screen gets a smaller window (the panels scale to fit), placed so its title bar is reachable.
    const njin::vec2 monitor = njin::monitor_size(*ctx);
    if (monitor.x > 0 && monitor.y > 0 && (monitor.x < 1700 + 40 || monitor.y < 960 + 80)) {
      njin::window_set_size(*ctx, {monitor.x < 1740 ? monitor.x - 40 : 1700.0f, monitor.y < 1040 ? monitor.y - 100 : 960.0f});
      njin::window_set_position(*ctx, {20, 40});
    }
    if (want_w >= 640 && want_h >= 400)
      njin::window_set_size(*ctx, {(float)want_w, (float)want_h});
  }
  if (!njin_tools::imgui_init(*ctx, true)) {
    std::fprintf(stderr, "njin_inspector: needs the SDL3/bgfx renderer\n");
    njin::destroy(ctx);
    return 1;
  }
  ImGui::StyleColorsDark();
  load_font();
  ImGui::GetIO().IniFilename = "njin_inspector.ini"; // remembers the layout
  njin_tools::imgui_systems(*ctx, ui);
  njin::run(*ctx);

  a.link.close();
  njin::net_close(a.connecting);
  njin_tools::imgui_shutdown(*ctx);
  njin::destroy(ctx);
  return 0;
}
