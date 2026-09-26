#include "njin_draw.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include <algorithm>
#include <raylib.h>

namespace njin {
namespace {
Color color_of(rgba c) {
  Color out{};
  to_raylib(c, out);
  return out;
}

Vector2 vec_of(vec2 v) { return Vector2{v.x, v.y}; }

Rectangle rect_of(rect r) { return Rectangle{r.pos.x, r.pos.y, r.size.x, r.size.y}; }
} // namespace

// Shapes

void draw_rect(const njin_ctx &, rect r, rgba color) {
  DrawRectangleRec(rect_of(r), color_of(color));
}

void draw_rect_lines(const njin_ctx &, rect r, f32 thickness, rgba color) {
  DrawRectangleLinesEx(rect_of(r), thickness, color_of(color));
}

void draw_rect_rotated(const njin_ctx &, vec2 center, vec2 size, f32 rotation,
                       rgba color) {
  // DrawRectanglePro places the rectangle's origin point at (x, y) and turns
  // it around that point; an origin of half the size makes (x, y) the centre.
  DrawRectanglePro(Rectangle{center.x, center.y, size.x, size.y},
                   Vector2{size.x * 0.5f, size.y * 0.5f}, rotation,
                   color_of(color));
}

void draw_circle(const njin_ctx &, vec2 center, f32 radius, rgba color) {
  DrawCircleV(vec_of(center), radius, color_of(color));
}

void draw_circle_lines(const njin_ctx &, vec2 center, f32 radius,
                       f32 thickness, rgba color) {
  const f32 inner = radius - thickness > 0.0f ? radius - thickness : 0.0f;
  // 0 segments lets raylib pick enough for the radius.
  DrawRing(vec_of(center), inner, radius, 0.0f, 360.0f, 0, color_of(color));
}

void draw_line(const njin_ctx &, vec2 a, vec2 b, f32 thickness, rgba color) {
  DrawLineEx(vec_of(a), vec_of(b), thickness, color_of(color));
}

void draw_triangle(const njin_ctx &, vec2 a, vec2 b, vec2 c, rgba color) {
  // raylib culls triangles whose vertices are not counter-clockwise on screen
  // (y down), which is cross < 0 there. Swap two vertices for the other order
  // so callers never have to think about winding.
  if (cross(b - a, c - a) > 0.0f)
    DrawTriangle(vec_of(a), vec_of(c), vec_of(b), color_of(color));
  else
    DrawTriangle(vec_of(a), vec_of(b), vec_of(c), color_of(color));
}

// Text

font_handle font_load(njin_ctx &ctx, const char *path, i32 size) {
  return font_store_load(ctx.font, path, size);
}

void font_unload(njin_ctx &ctx, font_handle font) {
  font_store_unload(ctx.font, font);
}

void draw_text(const njin_ctx &ctx, const char *text, vec2 pos, f32 size,
               rgba color, font_handle font) {
  if (text == nullptr)
    return;
  DrawTextEx(font_store_get(ctx.font, font), text, vec_of(pos), size,
             font_store_spacing(font, size), color_of(color));
}

vec2 text_measure(const njin_ctx &ctx, const char *text, f32 size,
                  font_handle font) {
  if (text == nullptr)
    return vec2{0.0f, 0.0f};
  const Vector2 m = MeasureTextEx(font_store_get(ctx.font, font), text, size,
                                  font_store_spacing(font, size));
  return vec2{m.x, m.y};
}

std::vector<std::string> text_wrap(const njin_ctx &ctx, const char *text, f32 size,
                                   f32 max_width, font_handle font) {
  std::vector<std::string> lines;
  if (text == nullptr)
    return lines;
  std::string paragraph;
  const auto flush_paragraph = [&]() {
    std::string line;
    usize i = 0;
    while (i <= paragraph.size()) {
      const usize end = std::min(paragraph.find(' ', i), paragraph.size());
      const std::string word = paragraph.substr(i, end - i);
      const std::string trial = line.empty() ? word : line + " " + word;
      if (!line.empty() && text_measure(ctx, trial.c_str(), size, font).x > max_width) {
        lines.push_back(line);
        line = word;
      } else {
        line = trial;
      }
      i = end + 1;
    }
    lines.push_back(line);
    paragraph.clear();
  };
  for (const char *c = text; *c != '\0'; c++) {
    if (*c == '\n')
      flush_paragraph();
    else
      paragraph.push_back(*c);
  }
  flush_paragraph();
  return lines;
}

vec2 draw_text_wrapped(const njin_ctx &ctx, const char *text, vec2 pos, f32 size,
                       f32 max_width, rgba color, font_handle font, f32 line_spacing) {
  vec2 extent{};
  f32 y = pos.y;
  const std::vector<std::string> lines = text_wrap(ctx, text, size, max_width, font);
  for (usize i = 0; i < lines.size(); i++) {
    const vec2 m = text_measure(ctx, lines[i].c_str(), size, font);
    draw_text(ctx, lines[i].c_str(), {pos.x, y}, size, color, font);
    extent.x = std::max(extent.x, m.x);
    extent.y = (y - pos.y) + m.y;
    y += (m.y > 0.0f ? m.y : size) * line_spacing;
  }
  return extent;
}

// Textures

void texture_set_filter(njin_ctx &ctx, texture_handle handle,
                        texture_filter filter) {
  texture_store_set_filter(ctx.texture, handle, filter);
}

void texture_draw_ex(const njin_ctx &ctx, texture_handle handle,
                     const texture_draw_desc &desc) {
  texture_store_draw_ex(ctx.texture, handle, desc);
}

// Blend and clip

void blend_begin(const njin_ctx &, blend_mode mode) {
  switch (mode) {
  case blend_additive:
    BeginBlendMode(BLEND_ADDITIVE);
    break;
  case blend_multiply:
    BeginBlendMode(BLEND_MULTIPLIED);
    break;
  case blend_alpha:
  default:
    BeginBlendMode(BLEND_ALPHA);
    break;
  }
}

void blend_end(const njin_ctx &) { EndBlendMode(); }

void clip_begin(const njin_ctx &, rect area) {
  BeginScissorMode((int)area.pos.x, (int)area.pos.y, (int)area.size.x,
                   (int)area.size.y);
}

void clip_end(const njin_ctx &) { EndScissorMode(); }
} // namespace njin
