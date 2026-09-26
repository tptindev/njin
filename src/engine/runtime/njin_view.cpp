#include "njin_view.h"
#include "njin2rl.h"
#include "njin_cfg.h"
#include "njin_ctx_impl.h"
#include "njin_window.h"
#include <algorithm>
#include <cmath>
#include <rlgl.h>

namespace njin {
view_state::~view_state() {
  if (IsRenderTextureValid(target))
    UnloadRenderTexture(target);
}

void view_frame_begin(view_state &view) {
  if (!view_active(view)) {
    view.scale = 1.0f;
    view.offset = {};
    return;
  }
  const f32 w = (f32)GetScreenWidth();
  const f32 h = (f32)GetScreenHeight();
  f32 s = std::min(w / view.size.x, h / view.size.y);
  // Whole multiples keep every virtual pixel the same size on screen. A window
  // smaller than the virtual size still gets the fractional fit.
  if (view.integer_scale && s >= 1.0f)
    s = std::floor(s);
  view.scale = s > 0.0f ? s : 1.0f;
  view.offset = {std::floor((w - view.size.x * view.scale) * 0.5f),
                 std::floor((h - view.size.y * view.scale) * 0.5f)};
}

void view_map_mouse(const view_state &view, vec2 &pos, vec2 &motion) {
  if (!view_active(view))
    return;
  pos = (pos - view.offset) / view.scale;
  motion = motion / view.scale;
}

void view_draw_begin(view_state &view, Color clear) {
  view.drawing = false;
  view.world_depth = 0;
  view.offscreen_depth = 0;
  view.text_layer.clear();
  view.occluders.clear();
  view.clip = {};
  if (!view_active(view))
    return;
  const i32 w = (i32)view.size.x;
  const i32 h = (i32)view.size.y;
  if (!IsRenderTextureValid(view.target) || view.target.texture.width != w ||
      view.target.texture.height != h) {
    if (IsRenderTextureValid(view.target))
      UnloadRenderTexture(view.target);
    view.target = LoadRenderTexture(w, h);
    if (!IsRenderTextureValid(view.target))
      return;
    SetTextureFilter(view.target.texture, TEXTURE_FILTER_POINT);
  }
  view.drawing = true;
  BeginTextureMode(view.target);
  ClearBackground(clear);
}

void view_text_occlude(const view_state &view, rect area, rgba color, bool hide) {
  if (view.text_layer.empty() || !view_text_deferred(view) || color.a <= 0.0f)
    return;
  view.occluders.push_back(text_occluder{area, color, hide});
}

namespace {
// The UI pass draws inside the image only, as the virtual target used to. The
// transform is rlgl's own (applied to each vertex as it is added), not a 2D
// camera's (the modelview matrix): a line of text can then be drawn without it
// by pushing an identity, with no flush, and a game's own camera in the pass
// (BeginMode2D) still works over it.
void ui_enter(const view_state &view) {
  BeginScissorMode((int)view.offset.x, (int)view.offset.y, (int)(view.size.x * view.scale),
                   (int)(view.size.y * view.scale));
  rlPushMatrix();
  rlTranslatef(view.offset.x, view.offset.y, 0.0f);
  rlScalef(view.scale, view.scale, 1.0f);
}

void ui_leave() {
  rlPopMatrix();
  EndScissorMode();
}
} // namespace

bool view_ui_begin(view_state &view) {
  if (!view.smooth_ui || !view.drawing || view.scale == 1.0f)
    return false;
  view_draw_end(view);
  view.ui_window = true;
  ui_enter(view);
  return true;
}

void view_ui_end(view_state &view) {
  if (!view.ui_window)
    return;
  ui_leave();
  view.ui_window = false;
}

void view_ui_suspend(const view_state &view) {
  if (!view.ui_window)
    return;
  ui_leave();
}

void view_rebind(const view_state &view) {
  if (view.ui_window) {
    ui_enter(view);
    return;
  }
  if (view.drawing)
    BeginTextureMode(view.target);
}

void view_draw_end(view_state &view) {
  if (!view.drawing)
    return;
  view.drawing = false;
  EndTextureMode();
  Color bars{};
  to_raylib(view.bars, bars);
  ClearBackground(bars);
  const Texture2D &tex = view.target.texture;
  const Rectangle source{0.0f, 0.0f, (f32)tex.width, -(f32)tex.height};
  const Rectangle dest{view.offset.x, view.offset.y, view.size.x * view.scale,
                       view.size.y * view.scale};
  DrawTexturePro(tex, source, dest, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
}

// Public API (njin_window.h / njin_cfg.h).

void window_set_virtual_size(njin_ctx &ctx, vec2 size, bool integer_scale) {
  ctx.view.size = size.x >= 1.0f && size.y >= 1.0f ? vec2{std::floor(size.x), std::floor(size.y)}
                                                   : vec2{};
  ctx.view.integer_scale = integer_scale;
  view_frame_begin(ctx.view);
}

void window_set_bar_color(njin_ctx &ctx, rgba color) { ctx.view.bars = color; }

vec2 window_size(const njin_ctx &) { return {(f32)GetScreenWidth(), (f32)GetScreenHeight()}; }

rect window_viewport(const njin_ctx &ctx) {
  if (!view_active(ctx.view))
    return rect{{0.0f, 0.0f}, window_size(ctx)};
  return rect{ctx.view.offset, ctx.view.size * ctx.view.scale};
}

vec2 screen_size(const njin_ctx &ctx) {
  if (view_active(ctx.view))
    return ctx.view.size;
  return {(f32)GetScreenWidth(), (f32)GetScreenHeight()};
}
} // namespace njin
