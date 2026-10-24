#include "njin_draw.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_font.h"
#include "njin_shader.h"
#include "njin_texture.h"
#include "njin_view.h"
#include "njin_log.h"
#include "njin_render.h"
#include "njin_sdf2d.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <utility>
#include <raylib.h>
#include <rlgl.h>

namespace njin {
namespace {
Color color_of(rgba c) {
  Color out{};
  to_raylib(c, out);
  return out;
}

Vector2 vec_of(vec2 v) { return Vector2{v.x, v.y}; }

Rectangle rect_of(rect r) { return Rectangle{r.pos.x, r.pos.y, r.size.x, r.size.y}; }

bool has_area(rect r) { return r.size.x > 0.0f && r.size.y > 0.0f; }

rect intersect(rect a, rect b) {
  const f32 x0 = std::max(a.pos.x, b.pos.x);
  const f32 y0 = std::max(a.pos.y, b.pos.y);
  const f32 x1 = std::min(a.pos.x + a.size.x, b.pos.x + b.size.x);
  const f32 y1 = std::min(a.pos.y + a.size.y, b.pos.y + b.size.y);
  return rect{{x0, y0}, {std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)}};
}

// The parts of `a` that `b` does not cover: up to four rectangles.
void subtract(rect a, rect b, std::vector<rect> &out) {
  const rect in = intersect(a, b);
  if (!has_area(in)) {
    out.push_back(a);
    return;
  }
  const f32 ax1 = a.pos.x + a.size.x, ay1 = a.pos.y + a.size.y;
  const f32 ix1 = in.pos.x + in.size.x, iy1 = in.pos.y + in.size.y;
  if (in.pos.y > a.pos.y)
    out.push_back(rect{a.pos, {a.size.x, in.pos.y - a.pos.y}});
  if (iy1 < ay1)
    out.push_back(rect{{a.pos.x, iy1}, {a.size.x, ay1 - iy1}});
  if (in.pos.x > a.pos.x)
    out.push_back(rect{{a.pos.x, in.pos.y}, {in.pos.x - a.pos.x, in.size.y}});
  if (ix1 < ax1)
    out.push_back(rect{{ix1, in.pos.y}, {ax1 - ix1, in.size.y}});
}

// A part of a queued line, and the colour it is drawn in.
struct text_piece {
  rect area;
  rgba color;
};

// What is left of a queued line after everything drawn over it since: the
// piece under a translucent panel keeps the panel's share of the colour, the
// piece under an opaque one is gone.
std::vector<text_piece> visible_pieces(const view_state &view, const queued_text &q) {
  rect area = intersect(q.bounds, rect{{0.0f, 0.0f}, view.size});
  if (has_area(q.clip))
    area = intersect(area, q.clip);
  std::vector<text_piece> pieces;
  if (has_area(area))
    pieces.push_back({area, q.color});
  for (usize k = q.occluders; k < view.occluders.size() && !pieces.empty(); k++) {
    const text_occluder &o = view.occluders[k];
    std::vector<text_piece> next;
    for (const text_piece &p : pieces) {
      const rect under = intersect(p.area, o.area);
      if (!has_area(under)) {
        next.push_back(p);
        continue;
      }
      std::vector<rect> rest;
      subtract(p.area, o.area, rest);
      for (const rect &r : rest)
        next.push_back({r, p.color});
      if (o.hide || o.color.a >= 0.999f)
        continue;
      const f32 a = std::min(o.color.a, 1.0f);
      next.push_back({under, rgba{p.color.r + (o.color.r - p.color.r) * a,
                                  p.color.g + (o.color.g - p.color.g) * a,
                                  p.color.b + (o.color.b - p.color.b) * a, p.color.a}});
    }
    pieces.swap(next);
  }
  return pieces;
}
} // namespace

// Shapes

void draw_rect(const context &ctx, rect r, rgba color) {
  DrawRectangleRec(rect_of(r), color_of(color));
  view_text_occlude(ctx.view, r, color);
}

void draw_rect_lines(const context &, rect r, f32 thickness, rgba color) {
  DrawRectangleLinesEx(rect_of(r), thickness, color_of(color));
}

namespace {
// A radius in pixels as raylib's roundness: a share of half the shorter side.
f32 roundness_of(rect r, f32 radius) {
  const f32 shorter = std::min(r.size.x, r.size.y);
  return shorter > 0.0f ? std::clamp(2.0f * radius / shorter, 0.0f, 1.0f) : 0.0f;
}
} // namespace

void draw_rect_rounded(const context &ctx, rect r, f32 radius, rgba color) {
  if (!sdf2d_rounded_rect(ctx, r, radius, 0.0f, color)) {
    if (radius <= 0.0f)
      DrawRectangleRec(rect_of(r), color_of(color));
    else
      DrawRectangleRounded(rect_of(r), roundness_of(r, radius), 0, color_of(color));
  }
  view_text_occlude(ctx.view, r, color);
}

void draw_rect_rounded_lines(const context &ctx, rect r, f32 radius, f32 thickness, rgba color) {
  if (sdf2d_rounded_rect(ctx, r, radius, thickness, color))
    return;
  // raylib draws the line outside the rectangle it is given: give it the one
  // `thickness` inside `r`, its corners rounder by as much less.
  const f32 t = std::max(0.0f, std::min(thickness, std::min(r.size.x, r.size.y) * 0.5f));
  const rect in{{r.pos.x + t, r.pos.y + t}, {r.size.x - 2.0f * t, r.size.y - 2.0f * t}};
  DrawRectangleRoundedLinesEx(rect_of(in), roundness_of(in, std::max(0.0f, radius - t)), 0, t, color_of(color));
}

void draw_rect_rotated(const context &, vec2 center, vec2 size, f32 rotation,
                       rgba color) {
  // DrawRectanglePro places the rectangle's origin point at (x, y) and turns
  // it around that point; an origin of half the size makes (x, y) the centre.
  DrawRectanglePro(Rectangle{center.x, center.y, size.x, size.y},
                   Vector2{size.x * 0.5f, size.y * 0.5f}, rotation,
                   color_of(color));
}

void draw_circle(const context &ctx, vec2 center, f32 radius, rgba color) {
  // As many sides as keep the outline within half a pixel of the true circle,
  // at the size it reaches the screen (camera zoom and supersampling), up to
  // raylib's 36: a circle a few pixels wide takes 6, not 36 (a city of trees
  // seen from far is thousands of them).
  const Matrix view = rlGetMatrixModelview();
  const f32 px = radius * std::hypot(view.m0, view.m1) * (f32)std::max(ctx.view->render_scale, 1);
  i32 sides = 36;
  if (px < 40.0f) {
    const f32 k = 1.0f - 0.5f / std::max(px, 0.75f);
    const f32 step = std::acos(std::clamp(2.0f * k * k - 1.0f, -1.0f, 1.0f));
    sides = std::clamp((i32)std::ceil(2.0f * PI / std::max(step, 1e-3f)), 6, 36);
  }
  DrawCircleSector(vec_of(center), radius, 0.0f, 360.0f, sides, color_of(color));
}

void draw_circle_lines(const context &, vec2 center, f32 radius,
                       f32 thickness, rgba color) {
  const f32 inner = radius - thickness > 0.0f ? radius - thickness : 0.0f;
  // 0 segments lets raylib pick enough for the radius.
  DrawRing(vec_of(center), inner, radius, 0.0f, 360.0f, 0, color_of(color));
}

void draw_line(const context &, vec2 a, vec2 b, f32 thickness, rgba color) {
  DrawLineEx(vec_of(a), vec_of(b), thickness, color_of(color));
}

namespace {
std::vector<f32> alpha_layers; // draw_alpha_push(): each layer's alpha
} // namespace

void draw_alpha_push(const context &, f32 alpha) {
  alpha_layers.push_back(std::clamp(alpha, 0.0f, 1.0f));
  draw_alpha_now *= alpha_layers.back();
}

void draw_alpha_pop(const context &) {
  if (alpha_layers.empty()) {
    NJIN_WARN("draw_alpha_pop without draw_alpha_push");
    return;
  }
  alpha_layers.pop_back();
  draw_alpha_now = 1.0f;
  for (const f32 a : alpha_layers)
    draw_alpha_now *= a;
}

void draw_offset_push(const context &, vec2 offset, f32 scale) {
  rlPushMatrix();
  rlTranslatef(offset.x, offset.y, 0.0f);
  rlScalef(scale, scale, 1.0f);
}

void draw_offset_pop(const context &) { rlPopMatrix(); }

void draw_triangle(const context &, vec2 a, vec2 b, vec2 c, rgba color) {
  // raylib culls triangles whose vertices are not counter-clockwise on screen
  // (y down), which is cross < 0 there. Swap two vertices for the other order
  // so callers never have to think about winding.
  if (cross(b - a, c - a) > 0.0f)
    DrawTriangle(vec_of(a), vec_of(c), vec_of(b), color_of(color));
  else
    DrawTriangle(vec_of(a), vec_of(b), vec_of(c), color_of(color));
}

namespace {
// A textured triangle from texture `id`, `page` taking a corner's uv to the texture's.
template <class Page>
void textured_triangle(u32 id, vec2 a, vec2 b, vec2 c, vec2 uv_a, vec2 uv_b, vec2 uv_c, rgba tint, Page page) {
  // Counter-clockwise on screen, as draw_triangle().
  if (cross(b - a, c - a) > 0.0f) {
    std::swap(b, c);
    std::swap(uv_b, uv_c);
  }
  const Color col = color_of(tint);
  rlSetTexture(id);
  rlBegin(RL_TRIANGLES);
  rlColor4ub(col.r, col.g, col.b, col.a);
  for (const auto &[p, uv] : {std::pair{a, uv_a}, std::pair{b, uv_b}, std::pair{c, uv_c}}) {
    const vec2 t = page(uv);
    rlTexCoord2f(t.x, t.y);
    rlVertex2f(p.x, p.y);
  }
  rlEnd();
  rlSetTexture(0);
}
} // namespace

void draw_triangle_textured(const context &ctx, texture_handle texture, vec2 a, vec2 b, vec2 c, vec2 uv_a,
                            vec2 uv_b, vec2 uv_c, rgba tint) {
  const texture_slot *slot = texture_slot_of(ctx.texture, texture);
  if (slot == nullptr || slot->texture.width <= 0 || slot->texture.height <= 0)
    return;
  // The image's own 0..1 into the texture's (a packed image is a part of its page).
  const Rectangle area = texture_area(*slot);
  const f32 w = static_cast<f32>(slot->texture.width), h = static_cast<f32>(slot->texture.height);
  textured_triangle(slot->texture.id, a, b, c, uv_a, uv_b, uv_c, tint, [&](vec2 uv) {
    return vec2{(area.x + uv.x * area.width) / w, (area.y + uv.y * area.height) / h};
  });
}

void draw_triangle_textured(const context &ctx, render_texture_handle texture, vec2 a, vec2 b, vec2 c,
                            vec2 uv_a, vec2 uv_b, vec2 uv_c, rgba tint) {
  const render_texture_slot *slot = render_texture_slot_of(ctx.render_texture, texture);
  if (slot == nullptr)
    return;
  // A render texture is stored upside down.
  textured_triangle(slot->target.texture.id, a, b, c, uv_a, uv_b, uv_c, tint,
                    [](vec2 uv) { return vec2{uv.x, 1.0f - uv.y}; });
}

// Text

font_handle font_load(context &ctx, const char *path, i32 size, font_style style) {
  return font_store_load(ctx.font, path, size, style == font_pixel);
}

void font_set_style(context &ctx, font_handle font, font_style style) {
  font_store_set_pixel(ctx.font, font, style == font_pixel);
}

void font_unload(context &ctx, font_handle font) {
  font_store_unload(ctx.font, font);
}

void font_set_default(context &ctx, font_handle font) {
  font_store_set_default(ctx.font, font);
}

void font_add_chars(context &ctx, font_handle font, const char *text) {
  font_store_add_chars(ctx.font, font, text);
}

namespace {
// Text in the UI pass: baked at size * scale and drawn in window pixels, with
// the pass's transform put aside. A glyph advance is a whole number of pixels,
// so the same line is a little narrower or wider at another size. The virtual
// size is what the game measured and aligned with: stretch the gaps until the
// line is as wide as that, so centred and right-aligned text stays where it was
// put.
// A line drawn under a transform of `scale` and `offset` (the smooth UI pass's,
// the UI canvas's), baked at its size on the window and drawn without it.
void draw_text_window(const context &ctx, const char *text, vec2 pos, f32 size, rgba color,
                      font_handle font, vec2 offset, f32 scale) {
  const Font *atlas = font_store_atlas(ctx.font, font, font_px(size * scale));
  const Font *layout = font_store_atlas(ctx.font, font, font_px(size));
  if (atlas == nullptr || layout == nullptr)
    return;
  f32 spacing = 0.0f;
  int glyphs = 0;
  bool one_line = true;
  for (const char *c = text; *c != '\0'; c++)
    if (*c == '\n')
      one_line = false;
  if (one_line)
    glyphs = GetCodepointCount(text);
  if (glyphs > 1) {
    const f32 want = MeasureTextEx(*layout, text, (f32)layout->baseSize, 0.0f).x * scale;
    const f32 have = MeasureTextEx(*atlas, text, (f32)atlas->baseSize, 0.0f).x;
    spacing = (want - have) / (f32)(glyphs - 1);
  }
  const Vector2 at{std::round(offset.x + pos.x * scale), std::round(offset.y + pos.y * scale)};
  rlPushMatrix();
  rlLoadIdentity();
  DrawTextEx(*atlas, text, at, (f32)atlas->baseSize, spacing, color_of(color));
  rlPopMatrix();
}
} // namespace

void draw_text(const context &ctx, const char *text, vec2 pos, f32 size,
               rgba color, font_handle font) {
  if (text == nullptr || text[0] == '\0' || size < 1.0f)
    return;
  // The pixel style is meant to be scaled with the rest of the pixels, by the
  // same nearest filter, so it stays in the virtual image.
  if (view_text_deferred(ctx.view) && !font_store_is_pixel(ctx.font, font)) {
    const vec2 m = text_measure(ctx, text, size, font);
    // Room for the marks above and below the line and the overhang of the glyphs.
    const f32 margin = size * 0.3f;
    ctx.view->text_layer.push_back(queued_text{
        text, pos, size, color, font,
        rect{{pos.x - margin, pos.y - margin}, {m.x + 2.0f * margin, m.y + 2.0f * margin}},
        ctx.view->clip, ctx.view->occluders.size()});
    return;
  }
  if (ctx.view->ui_window && ctx.view->offscreen_depth == 0 && !font_store_is_pixel(ctx.font, font)) {
    draw_text_window(ctx, text, pos, size, color, font, ctx.view->offset, ctx.view->scale);
    return;
  }
  if (ctx.view->canvas_on && !font_store_is_pixel(ctx.font, font)) {
    draw_text_window(ctx, text, pos, size, color, font, {}, ctx.view->canvas_scale);
    return;
  }
  const Font *atlas = font_store_atlas(ctx.font, font, font_px(size));
  if (atlas == nullptr)
    return;
  // Rounded to the pixel grid: half a pixel of offset is half a pixel of blur
  // on a ten-pixel letter, and centred text lands on halves constantly.
  DrawTextEx(*atlas, text, Vector2{std::round(pos.x), std::round(pos.y)},
             (f32)atlas->baseSize, 0.0f, color_of(color));
}

vec2 text_measure(const context &ctx, const char *text, f32 size,
                  font_handle font) {
  if (text == nullptr)
    return vec2{0.0f, 0.0f};
  // Through the atlas draw_text will use, or a centred line would be centred
  // on a width nothing renders at.
  const Font *atlas = font_store_atlas(ctx.font, font, font_px(size));
  if (atlas == nullptr)
    return vec2{0.0f, 0.0f};
  const Vector2 m = MeasureTextEx(*atlas, text, (f32)atlas->baseSize, 0.0f);
  return vec2{m.x, m.y};
}

void text_layer_flush(context &ctx) {
  view_state &view = ctx.view;
  // Swapped, not moved out, so both buffers keep their room between frames.
  thread_local std::vector<queued_text> queue;
  queue.clear();
  queue.swap(view.text_layer);
  if (queue.empty() || !view_active(view)) {
    view.occluders.clear();
    return;
  }
  for (const queued_text &q : queue) {
    const Font *atlas = font_store_atlas(ctx.font, q.font, font_px(q.size * view.scale));
    const Font *layout = font_store_atlas(ctx.font, q.font, font_px(q.size));
    if (atlas == nullptr || layout == nullptr)
      continue;
    // A glyph advance is a whole number of pixels, so the same line is a little
    // narrower or wider at another size. The virtual size is what the game
    // measured and aligned with: stretch the gaps until the line is as wide as
    // that, so centred and right-aligned text stays where it was put.
    f32 spacing = 0.0f;
    const bool one_line = q.text.find('\n') == std::string::npos;
    const int glyphs = one_line ? GetCodepointCount(q.text.c_str()) : 0;
    if (glyphs > 1) {
      const f32 want = MeasureTextEx(*layout, q.text.c_str(), (f32)layout->baseSize, 0.0f).x * view.scale;
      const f32 have = MeasureTextEx(*atlas, q.text.c_str(), (f32)atlas->baseSize, 0.0f).x;
      spacing = (want - have) / (f32)(glyphs - 1);
    }
    const Vector2 at{std::round(view.offset.x + q.pos.x * view.scale),
                     std::round(view.offset.y + q.pos.y * view.scale)};
    // Each visible part of the line is drawn through a scissor of its own, in
    // window pixels, so the image's bars stay clean and what is drawn over the
    // line since it was queued stays over it.
    const std::vector<text_piece> pieces = visible_pieces(view, q);
    // Nothing over it, no clip, inside the image: the glyphs stay in `bounds`
    // without a scissor, and the line joins the batch instead of flushing it.
    if (pieces.size() == 1 && pieces[0].area.pos.x == q.bounds.pos.x && pieces[0].area.pos.y == q.bounds.pos.y &&
        pieces[0].area.size.x == q.bounds.size.x && pieces[0].area.size.y == q.bounds.size.y) {
      DrawTextEx(*atlas, q.text.c_str(), at, (f32)atlas->baseSize, spacing, color_of(pieces[0].color));
      continue;
    }
    for (const text_piece &p : pieces) {
      const int x0 = (int)std::floor(view.offset.x + p.area.pos.x * view.scale);
      const int y0 = (int)std::floor(view.offset.y + p.area.pos.y * view.scale);
      const int x1 = (int)std::ceil(view.offset.x + (p.area.pos.x + p.area.size.x) * view.scale);
      const int y1 = (int)std::ceil(view.offset.y + (p.area.pos.y + p.area.size.y) * view.scale);
      BeginScissorMode(x0, y0, x1 - x0, y1 - y0);
      DrawTextEx(*atlas, q.text.c_str(), at, (f32)atlas->baseSize, spacing, color_of(p.color));
      EndScissorMode();
    }
  }
  view.occluders.clear();
}

std::vector<std::string> text_wrap(const context &ctx, const char *text, f32 size,
                                   f32 max_width, font_handle font) {
  std::vector<std::string> lines;
  if (text == nullptr)
    return lines;
  // Chinese and Japanese have no spaces: a line may break between any two of
  // their characters, but not before a closing mark (、。」) or after an opening one.
  const auto cjk = [](i32 c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF);
  };
  const auto closing = [](i32 c) {
    return c == 0x3001 || c == 0x3002 || c == 0x300D || c == 0x300F || c == 0x3011 || c == 0xFF01 ||
           c == 0xFF09 || c == 0xFF0C || c == 0xFF0E || c == 0xFF1A || c == 0xFF1B || c == 0xFF1F ||
           c == 0x30FC || c == 0x2026;
  };
  const auto opening = [](i32 c) { return c == 0x300C || c == 0x300E || c == 0x3010 || c == 0xFF08; };
  std::string paragraph;
  const auto flush_paragraph = [&]() {
    // The pieces a line may break between, each with what joins it to the
    // one before: a space, or nothing between two CJK characters.
    struct piece {
      std::string text;
      bool spaced = false;
    };
    std::vector<piece> pieces(1);
    i32 last = 0; // the character before, in this piece
    for (usize i = 0; i < paragraph.size();) {
      int bytes = 1;
      const i32 c = GetCodepointNext(paragraph.c_str() + i, &bytes);
      if (c == ' ') {
        pieces.push_back({"", true});
        last = 0;
      } else {
        if ((cjk(last) || cjk(c)) && !closing(c) && !opening(last) && !pieces.back().text.empty())
          pieces.push_back({"", false});
        last = c;
      }
      if (c != ' ')
        pieces.back().text.append(paragraph, i, (usize)bytes);
      i += (usize)std::max(1, bytes);
    }
    std::string line;
    for (const piece &p : pieces) {
      const std::string trial = line.empty() ? p.text : line + (p.spaced ? " " : "") + p.text;
      if (!line.empty() && text_measure(ctx, trial.c_str(), size, font).x > max_width) {
        lines.push_back(line);
        line = p.text;
      } else {
        line = trial;
      }
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

vec2 draw_text_wrapped(const context &ctx, const char *text, vec2 pos, f32 size,
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

void texture_set_filter(context &ctx, texture_handle handle,
                        texture_filter filter) {
  texture_store_set_filter(ctx.texture, handle, filter);
}

void texture_draw_ex(const context &ctx, texture_handle handle,
                     const texture_draw_desc &desc) {
  const texture_slot *slot = texture_slot_of(ctx.texture, handle);
  // A material shader (texture_set_shader) auto-binds only when nothing else
  // is already bound: an explicit shader_begin() (the game's own choice) wins.
  const bool material = slot != nullptr && slot->shader.id != 0 && ctx.shader->active.id == 0;
  if (material)
    shader_begin(ctx, slot->shader);
  texture_store_draw_ex(ctx.texture, handle, desc);
  if (material)
    shader_end(ctx);
}

// Blend and clip

void blend_begin(const context &, blend_mode mode) {
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

void blend_end(const context &) { EndBlendMode(); }

void clip_begin(const context &ctx, rect area) {
  ctx.view->clip = area;
  if (ctx.view->canvas_on) {
    // Scissor works in window pixels: the canvas's area times its scale.
    const f32 k = ctx.view->canvas_scale;
    BeginScissorMode((int)std::floor(area.pos.x * k), (int)std::floor(area.pos.y * k),
                     (int)std::ceil(area.size.x * k), (int)std::ceil(area.size.y * k));
    return;
  }
  if (ctx.view->ui_window) {
    // Scissor works in window pixels, whatever the transform is.
    const view_state &v = ctx.view;
    const rect image{{0.0f, 0.0f}, v.size};
    const rect in = intersect(area, image);
    BeginScissorMode((int)std::floor(v.offset.x + in.pos.x * v.scale),
                     (int)std::floor(v.offset.y + in.pos.y * v.scale),
                     (int)std::ceil(in.size.x * v.scale), (int)std::ceil(in.size.y * v.scale));
    return;
  }
  BeginScissorMode((int)area.pos.x, (int)area.pos.y, (int)area.size.x,
                   (int)area.size.y);
}

void clip_end(const context &ctx) {
  ctx.view->clip = {};
  EndScissorMode();
  // The UI pass stays inside the image.
  if (ctx.view->ui_window) {
    const view_state &v = ctx.view;
    BeginScissorMode((int)v.offset.x, (int)v.offset.y, (int)(v.size.x * v.scale),
                     (int)(v.size.y * v.scale));
  }
}
} // namespace njin
