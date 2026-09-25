#include <njin.h>

namespace {
njin::font_handle title_font;

void load(njin::njin_ctx &ctx) {
  // Font có tiếng Việt. Dựng ở cỡ 32 pixel: vẽ đúng cỡ này thì nét sắc nhất.
  title_font = njin::font_load(ctx, "assets/fonts/roboto.ttf", 32);
}

// Trong không gian thế giới: đi qua camera.
void draw_world(njin::njin_ctx &ctx) {
  using namespace njin;
  draw_rect(ctx, rect{{0, 0}, {200, 20}}, colors::gray);         // mặt đất
  draw_circle(ctx, {100, -30}, 20, colors::yellow);             // quả bóng
  draw_line(ctx, {0, -60}, {200, -60}, 2.0f, colors::white);    // dây
  draw_triangle(ctx, {150, 0}, {170, -40}, {190, 0}, colors::green);
  draw_rect_rotated(ctx, {40, -30}, {30, 30}, 45.0f, colors::red);

  // Blend cộng: các vòng sáng chồng lên nhau thì sáng hơn.
  blend_begin(ctx, blend_additive);
  draw_circle(ctx, {80, -100}, 30, rgba{1.0f, 0.5f, 0.1f, 0.5f});
  draw_circle(ctx, {110, -100}, 30, rgba{1.0f, 0.5f, 0.1f, 0.5f});
  blend_end(ctx);
}

// Trong không gian màn hình: UI, không bị camera dịch hay phóng.
void draw_ui(njin::njin_ctx &ctx) {
  using namespace njin;
  const vec2 screen = screen_size(ctx);

  // Chữ căn giữa: đo trước rồi lùi một nửa.
  const char *title = "Màn 1: Khởi đầu";
  const vec2 size = text_measure(ctx, title, 32, title_font);
  draw_text(ctx, title, {(screen.x - size.x) * 0.5f, 20}, 32, colors::white,
            title_font);

  // Font mặc định: chỉ ASCII, không cần nạp.
  draw_text(ctx, "FPS 60", {10, 10}, 20, colors::green);

  // Khung cuộn: chỉ vẽ bên trong vùng 300x100.
  const rect panel{{20, 80}, {300, 100}};
  draw_rect(ctx, panel, rgba{0, 0, 0, 0.5f});
  clip_begin(ctx, panel);
  for (int i = 0; i < 10; i++)
    draw_text(ctx, "Dong chu dai", {30, 90 + i * 24.0f}, 20, colors::white);
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
