// The rmlui feature on the SDL3/bgfx renderer: a document with a data model, drawn on
// top of the frame. Checks, through screenshots and SDL events pushed as the window
// would send them:
//   - the document draws: its panel and button in their colours, where the RCSS puts them;
//   - {{coins}} follows the game's value (the text's pixels change when it changes);
//   - a click on the button reaches the game through data-event-click, and while the
//     mouse is over the button rml_wants_mouse() says the UI has it;
//   - data-if hides an element when its value turns false.
//   njin_rml_test <font.ttf> [renderer]
#include "njin.h"
#include "njin_rml.h"
#include "njin_renderer.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
int failures = 0;
void expect(bool ok, const char *what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++failures;
}

struct state {
  std::string dir;
  int frame = 0;
  njin::i64 coins = 3;
  bool shown = true, clicked = false, wanted = false;
  njin::rml_document_handle doc;
};
state g;

std::string shot(int n) { return g.dir + "/shot" + std::to_string(n) + ".png"; }

const unsigned char *pixel(const njin::RenderImageAsset &img, int x, int y) {
  return img.pixels.data() + ((size_t)y * img.width + (size_t)x) * 4;
}
bool near(const unsigned char *p, int r, int g_, int b) {
  return std::abs(p[0] - r) < 12 && std::abs(p[1] - g_) < 12 && std::abs(p[2] - b) < 12;
}
int changed(const njin::RenderImageAsset &a, const njin::RenderImageAsset &b, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      const unsigned char *p = pixel(a, x, y), *q = pixel(b, x, y);
      n += std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]) > 40;
    }
  return n;
}

void push_mouse(njin::context &ctx, float x, float y, bool down, bool up) {
  SDL_Window *w = SDL_GetKeyboardFocus() ? SDL_GetKeyboardFocus() : SDL_GetMouseFocus();
  const SDL_WindowID id = w ? SDL_GetWindowID(w) : 0;
  (void)ctx;
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.windowID = id;
  e.motion.x = x;
  e.motion.y = y;
  SDL_PushEvent(&e);
  if (down || up) {
    SDL_Event b{};
    b.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    b.button.windowID = id;
    b.button.button = SDL_BUTTON_LEFT;
    b.button.down = down;
    b.button.clicks = 1;
    b.button.x = x;
    b.button.y = y;
    SDL_PushEvent(&b);
  }
}

void step(njin::context &ctx) {
  ++g.frame;
  switch (g.frame) {
  case 20: njin::screenshot(ctx, shot(1).c_str()); break;
  case 22: g.coins = 42; break;
  case 30: njin::screenshot(ctx, shot(2).c_str()); break;
  case 32: push_mouse(ctx, 120, 230, false, false); break; // over the button
  case 36: g.wanted = njin::rml_wants_mouse(ctx); break;
  case 37: push_mouse(ctx, 120, 230, true, false); break;
  case 38: push_mouse(ctx, 120, 230, false, true); break;
  case 44: push_mouse(ctx, 600, 400, false, false); break; // away from the document
  case 48:
    expect(!njin::rml_wants_mouse(ctx), "the mouse off the document is the game's");
    g.shown = false;
    break;
  case 56: njin::screenshot(ctx, shot(3).c_str()); break;
  case 60: njin::quit(ctx); break;
  default: break;
  }
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: njin_rml_test <font.ttf> [renderer]\n");
    return 2;
  }
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "njin-rml-test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  g.dir = dir.generic_string();
  std::filesystem::copy_file(argv[1], dir / "ui.ttf");
  std::ofstream(dir / "hud.rcss") << R"(
body { font-family: "Be Vietnam Pro"; font-size: 28px; color: #ffffff; width: 100%; height: 100%;
       pointer-events: none; }
#panel { position: absolute; left: 40px; top: 40px; width: 300px; height: 120px; background-color: #c81e1e; }
#coins { position: absolute; left: 10px; top: 10px; }
#ok { position: absolute; left: 40px; top: 200px; width: 160px; height: 60px; background-color: #1e3cc8;
      pointer-events: auto; text-align: center; }
#flag { position: absolute; left: 400px; top: 40px; width: 80px; height: 80px; background-color: #1ec83c; }
)";
  std::ofstream(dir / "hud.rml") << R"(<rml>
<head><link type="text/rcss" href="hud.rcss"/></head>
<body data-model="hud">
  <div id="panel"><span id="coins">Xu: {{coins}} đồng</span></div>
  <div id="ok" data-event-click="pressed">Đồng ý</div>
  <div id="flag" data-if="shown"></div>
</body>
</rml>)";

  njin::config cfg{.title = "njin rml test", .width = 800.0f, .height = 480.0f, .target_fps = 60.0f};
  cfg.clear_bg_color = njin::rgba{0.05f, 0.05f, 0.05f, 1.0f};
  cfg.audio = false;
  if (argc > 2) {
    const std::string r = argv[2];
    cfg.graphics_backend = r == "vulkan"         ? njin::GraphicsBackend::Vulkan
                           : r == "d3d12"        ? njin::GraphicsBackend::Direct3D12
                           : r == "bgfx-opengl"  ? njin::GraphicsBackend::BgfxOpenGL
                                                 : njin::GraphicsBackend::Auto;
  }
  njin::context *ctx = njin::create(cfg);
  expect(njin::rml_font_load(*ctx, (g.dir + "/ui.ttf").c_str()), "the font loads");
  const njin::rml_model_handle hud = njin::rml_model(*ctx, "hud");
  njin::rml_bind_int(*ctx, hud, "coins", [] { return g.coins; });
  njin::rml_bind_bool(*ctx, hud, "shown", [] { return g.shown; });
  njin::rml_bind_event(*ctx, hud, "pressed", [](njin::context &) { g.clicked = true; });
  g.doc = njin::rml_document_load(*ctx, (g.dir + "/hud.rml").c_str());
  expect((bool)g.doc && njin::rml_document_visible(*ctx, g.doc), "the document loads and shows");
  njin::mod_register(*ctx, njin::mod_desc{"rml_test", [](njin::context &c) {
                                            njin::ecs_register(c, njin::phase_update, step, "rml_test");
                                          }});
  njin::run(*ctx);
  njin::destroy(ctx);

  const njin::RenderImageAsset a = njin::renderer_load_image(shot(1).c_str());
  const njin::RenderImageAsset b = njin::renderer_load_image(shot(2).c_str());
  const njin::RenderImageAsset c = njin::renderer_load_image(shot(3).c_str());
  expect(a.width == 800 && a.height == 480, "screenshots at the window size");
  if (a.width == 800 && a.height == 480 && b.width == a.width && c.width == a.width) {
    expect(near(pixel(a, 300, 140), 200, 30, 30), "the panel is red where the RCSS puts it");
    expect(near(pixel(a, 190, 250), 30, 60, 200), "the button is blue where the RCSS puts it");
    expect(near(pixel(a, 440, 80), 30, 200, 60), "data-if shows the flag while its value is true");
    expect(near(pixel(a, 600, 400), 13, 13, 13), "outside the document the frame is untouched");
    const int text = changed(a, b, 50, 50, 330, 100);
    std::printf("      {{coins}} text: %d pixels changed\n", text);
    expect(text > 40, "{{coins}} follows the game's value");
    expect(changed(a, b, 400, 200, 800, 480) == 0, "nothing else changes");
    expect(!near(pixel(c, 440, 80), 30, 200, 60), "data-if hides the flag when its value turns false");
  }
  expect(g.wanted, "over the button, rml_wants_mouse() says the UI has the mouse");
  expect(g.clicked, "a click on the button reaches the game (data-event-click)");
  std::printf("rml: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
