#include "demo.h"

#include <algorithm>

namespace lighting_demo {
namespace {
constexpr rgba panel{0.0f, 0.0f, 0.0f, 0.66f};
constexpr rgba title_color{1.0f, 0.86f, 0.55f, 1.0f};
constexpr rgba text_color{0.90f, 0.92f, 0.98f, 1.0f};
constexpr rgba code_color{0.60f, 0.90f, 0.70f, 1.0f};
constexpr rgba keys_color{0.62f, 0.78f, 1.0f, 1.0f};
constexpr rgba outline_color{1.0f, 0.3f, 0.9f, 1.0f};
constexpr const char *prev_text = "< phòng trước";
constexpr const char *next_text = "phòng sau >";

bool inside(rect r, vec2 p) { return p.x >= r.pos.x && p.x < r.pos.x + r.size.x && p.y >= r.pos.y && p.y < r.pos.y + r.size.y; }

void draw_button(context &ctx, rect r, const char *text) {
  const bool hot = inside(r, mouse_pos(ctx));
  draw_rect(ctx, r, hot ? rgba{0.30f, 0.40f, 0.65f, 1.0f} : rgba{0.18f, 0.22f, 0.34f, 1.0f});
  draw_text(ctx, text, {r.pos.x + 8.0f, r.pos.y + 3.0f}, 16.0f, colors::white);
}
} // namespace

void nav_buttons(const context &ctx, rect &prev, rect &next) {
  const vec2 screen = screen_size(ctx);
  const f32 wn = text_measure(ctx, next_text, 16.0f).x + 16.0f;
  const f32 wp = text_measure(ctx, prev_text, 16.0f).x + 16.0f;
  next = rect{{screen.x - wn - 4.0f, screen.y - 23.0f}, {wn, 22.0f}};
  prev = rect{{next.pos.x - wp - 6.0f, screen.y - 23.0f}, {wp, 22.0f}};
}

// In the world, before the lighting: what the room draws itself (so it is lit like the rest), and the
// outlines of every occluder on top when key G is on.
void draw_world(context &ctx) {
  const room &r = room_at(demo.current);
  if (r.draw != nullptr)
    r.draw(ctx);
}

void hud(context &ctx) {
  demo.frame_ms += (delta_real(ctx) * 1000.0f - demo.frame_ms) * 0.05f;
  entt::registry &reg = world(ctx);

  // Key G: every occluder's shape, in the world, over the lit picture. What the lights actually see.
  if (demo.outlines) {
    for (auto [e, o, t] : reg.view<const light_occluder, const transform>().each()) {
      // The same maths as the engine: scale, then turn, then move by the transform.
      light_occluder screen = o;
      for (vec2 &p : screen.points)
        p = w2scr(ctx, t.pos + rotate(p * t.scale, t.rot));
      draw_occluder_shape(ctx, screen, transform{}, {0.0f, 0.0f, 0.0f, 0.0f}, outline_color);
    }
  }

  // The labels, over the lit picture so they can always be read.
  for (const label &l : demo.labels) {
    if (l.text.empty())
      continue;
    const vec2 at = w2scr(ctx, l.at);
    const vec2 size = text_measure(ctx, l.text.c_str(), 16.0f);
    draw_rect(ctx, rect{{at.x - size.x * 0.5f - 4.0f, at.y - 1.0f}, {size.x + 8.0f, size.y + 2.0f}}, {0.0f, 0.0f, 0.0f, 0.5f});
    draw_text(ctx, l.text.c_str(), {at.x - size.x * 0.5f, at.y}, 16.0f, l.color);
  }

  const vec2 screen = screen_size(ctx);
  const render_info info = render_info_get(ctx);
  const std::string status = fmt("%.0f FPS  %.2f ms   đèn đã vẽ %u   L ánh sáng %s   G viền vật chắn %s   H ẩn chữ",
                                 1000.0f / demo.frame_ms, demo.frame_ms, info.lights, demo.lit ? "bật" : "tắt",
                                 demo.outlines ? "bật" : "tắt");
  draw_rect(ctx, rect{{0.0f, screen.y - 24.0f}, {screen.x, 24.0f}}, panel);
  draw_text(ctx, status.c_str(), {10.0f, screen.y - 21.0f}, 16.0f, text_color);
  rect prev, next;
  nav_buttons(ctx, prev, next);
  draw_button(ctx, prev, prev_text);
  draw_button(ctx, next, next_text);
  if (!demo.help)
    return;

  const room &r = room_at(demo.current);
  draw_rect(ctx, rect{{0.0f, 0.0f}, {screen.x, 118.0f}}, panel);
  const std::string title = fmt("%d / %d   %s", demo.current + 1, room_count, r.title);
  draw_text(ctx, title.c_str(), {12.0f, 6.0f}, 22.0f, title_color);
  const char *nav = "Tab / Shift+Tab, 1..9 0, F1..F12: đổi phòng";
  draw_text(ctx, nav, {screen.x - text_measure(ctx, nav, 16.0f).x - 12.0f, 10.0f}, 16.0f, keys_color);
  draw_text(ctx, r.what, {12.0f, 34.0f}, 16.0f, text_color);
  draw_text(ctx, r.code, {12.0f, 74.0f}, 16.0f, code_color);
  if (r.keys[0] != '\0')
    draw_text(ctx, r.keys, {12.0f, 95.0f}, 16.0f, keys_color);
}
} // namespace lighting_demo
