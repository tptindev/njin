#include "njin_view.h"
#include "njin2rl.h"
#include "njin_cfg.h"
#include "njin_ctx_impl.h"
#include "njin_window.h"
#include <algorithm>
#include <cmath>
#include <rlgl.h>

namespace njin {
namespace {
// A GL texture side this big is safe on every desktop GPU njin targets (the
// GL 3.3 core minimum is 1024; real hardware from the last decade+ is 8192 or
// 16384). render_scale is clamped against it in njin_create() so a big window
// at a high render_scale cannot ask LoadRenderTexture() for a texture no GPU
// will create.
constexpr i32 max_render_scale_dim = 8192;
} // namespace

// Like raylib's BeginTextureMode(target), except the GL viewport spans
// `target`'s real (possibly supersampled) pixel size while the projection
// stays at `logical` size: draws keep using logical (window or virtual)
// coordinates exactly as without render_scale, and the GPU just rasterizes
// them at extra density. BeginTextureMode itself couples viewport and
// projection to the same size, so it cannot do this; the pieces it calls
// (rlgl.h) are public.
//
// Neither BeginMode2D nor EndMode2D touch the viewport or the projection
// matrix, only the modelview one (see raylib's rcore.c), so this survives the
// world camera's own transform untouched. Also used by camera.cpp for its own
// post-processing target, so the world stays supersampled when a game turns
// on a built-in post_fx or its own post shader.
void bind_view_target(const RenderTexture2D &target, vec2 logical) {
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(target.id);
  rlViewport(0, 0, target.texture.width, target.texture.height);
  rlSetFramebufferWidth(target.texture.width);
  rlSetFramebufferHeight(target.texture.height);
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlOrtho(0, logical.x, logical.y, 0, 0.0f, 1.0f);
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
}

i32 view_clamp_render_scale(i32 requested, vec2 window_size) {
  i32 scale = requested < 1 ? 1 : requested;
  while (scale > 1 && (window_size.x * (f32)scale > (f32)max_render_scale_dim ||
                       window_size.y * (f32)scale > (f32)max_render_scale_dim))
    scale /= 2;
  return scale;
}

view_state::~view_state() {
  if (IsRenderTextureValid(target))
    UnloadRenderTexture(target);
}

void view_frame_begin(view_state &view) {
  if (!view_has_virtual_size(view)) {
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
  const vec2 logical = view_logical_size(view);
  const i32 w = (i32)logical.x * view.render_scale;
  const i32 h = (i32)logical.y * view.render_scale;
  if (!IsRenderTextureValid(view.target) || view.target.texture.width != w ||
      view.target.texture.height != h) {
    if (IsRenderTextureValid(view.target))
      UnloadRenderTexture(view.target);
    view.target = LoadRenderTexture(w, h);
    if (!IsRenderTextureValid(view.target))
      return;
    // Supersampling needs a bilinear downscale to blend the extra density
    // away; at render_scale 1 (the default) this is the same point filter as
    // before, so pixel art virtual size stays crisp.
    SetTextureFilter(view.target.texture,
                     view.render_scale > 1 ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
  }
  view.drawing = true;
  bind_view_target(view.target, logical);
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
    bind_view_target(view.target, view_logical_size(view));
}

void view_draw_end(view_state &view) {
  if (!view.drawing)
    return;
  view.drawing = false;
  EndTextureMode();
  Color bars{};
  to_raylib(view.bars, bars);
  ClearBackground(bars);
  const vec2 logical = view_logical_size(view);
  const Texture2D &tex = view.target.texture;
  const Rectangle source{0.0f, 0.0f, (f32)tex.width, -(f32)tex.height};
  const Rectangle dest{view.offset.x, view.offset.y, logical.x * view.scale,
                       logical.y * view.scale};
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
  return rect{ctx.view.offset, view_logical_size(ctx.view) * ctx.view.scale};
}

vec2 screen_size(const njin_ctx &ctx) {
  if (view_active(ctx.view))
    return view_logical_size(ctx.view);
  return {(f32)GetScreenWidth(), (f32)GetScreenHeight()};
}
} // namespace njin
