#include <njin.h>

int main() {
  // Pixel art drawn on a 320 x 180 screen, the engine scales it to the window by whole multiples:
  // sharp pixels at every window size, the leftover space becomes a border.
  njin::njin_ctx *ctx = njin::njin_create({.title = "Pixel art",
                                           .width = 1280,
                                           .height = 720,
                                           .target_fps = 60,
                                           .resizable = true,
                                           .virtual_size = {320.0f, 180.0f},
                                           .integer_scale = true});
  // Can be toggled at runtime: window_set_virtual_size(*ctx, {0, 0}) to draw straight onto the window.
  njin::window_set_bar_color(*ctx, {0.05f, 0.05f, 0.08f, 1.0f});
  // screen_size() returns 320 x 180 and mouse_pos() is in virtual pixels.
  // window_size() and window_viewport() give the real size of the window.
  njin::njin_destroy(ctx);
}
