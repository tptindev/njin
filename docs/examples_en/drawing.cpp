#include <njin.h>

namespace {
njin::font_handle title_font;

void load(njin::njin_ctx &ctx) {
  // A font with Vietnamese support. Built at size 32 pixels: drawing at exactly this size gives the sharpest strokes.
  title_font = njin::font_load(ctx, "assets/fonts/roboto.ttf", 32);
}

// [pixel_text]
// A pixel font designed at 8 pixels: draw it at 8, 16, 24. Text always sits inside the virtual image.
void load_pixel_font(njin::njin_ctx &ctx) {
  const njin::font_handle pixel =
      njin::font_load(ctx, "assets/fonts/PressStart2P.ttf", 8, njin::font_pixel);
  njin::ui_style style = njin::ui_default_style();
  style.font = pixel;
  style.font_size = 8.0f;
  njin::ui_style_set(ctx, style);
  // Or change the default font: njin::font_set_style(ctx, {}, njin::font_pixel);
}
// [pixel_text]

// In world space: goes through the camera.
void draw_world(njin::njin_ctx &ctx) {
  using namespace njin;
  draw_rect(ctx, rect{{0, 0}, {200, 20}}, colors::gray);         // ground
  draw_circle(ctx, {100, -30}, 20, colors::yellow);             // ball
  draw_line(ctx, {0, -60}, {200, -60}, 2.0f, colors::white);    // rope
  draw_triangle(ctx, {150, 0}, {170, -40}, {190, 0}, colors::green);
  draw_rect_rotated(ctx, {40, -30}, {30, 30}, 45.0f, colors::red);

  // Additive blend: overlapping glows get brighter.
  blend_begin(ctx, blend_additive);
  draw_circle(ctx, {80, -100}, 30, rgba{1.0f, 0.5f, 0.1f, 0.5f});
  draw_circle(ctx, {110, -100}, 30, rgba{1.0f, 0.5f, 0.1f, 0.5f});
  blend_end(ctx);
}

// In screen space: UI, not shifted or zoomed by the camera.
void draw_ui(njin::njin_ctx &ctx) {
  using namespace njin;
  const vec2 screen = screen_size(ctx);

  // Centered text: measure first, then step back by half.
  const char *title = "Level 1: The Beginning";
  const vec2 size = text_measure(ctx, title, 32, title_font);
  draw_text(ctx, title, {(screen.x - size.x) * 0.5f, 20}, 32, colors::white,
            title_font);

  // The default font (JetBrains Mono, with Vietnamese support): no need to load it.
  draw_text(ctx, "FPS 60", {10, 10}, 20, colors::green);

  // Scroll frame: only draws inside the 300x100 area.
  const rect panel{{20, 80}, {300, 100}};
  draw_rect(ctx, panel, rgba{0, 0, 0, 0.5f});
  clip_begin(ctx, panel);
  for (int i = 0; i < 10; i++)
    draw_text(ctx, "Long line of text", {30, 90 + i * 24.0f}, 20, colors::white);
  clip_end(ctx);
  draw_rect_lines(ctx, panel, 2.0f, colors::yellow);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load);
  njin::ecs_register(ctx, njin::phase_render, draw_world);
  njin::ecs_register(ctx, njin::phase_post_render, draw_ui);
}
} // namespace

njin::mod_desc drawing_module() { return {.name = "drawing", .setup = setup}; }
