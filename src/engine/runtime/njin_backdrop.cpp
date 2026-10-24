#include "njin_backdrop.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_view.h"
#include "njin_draw_shapes.h"
#include "njin_gpu_caps.h"
#include "njin_log.h"
#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

namespace njin {
namespace {

constexpr i32 color_bits = 0x00004000; // GL_COLOR_BUFFER_BIT

// One direction of a 9-tap Gaussian, as the post chain's. `direction` is the
// step between taps in texture coordinates; `area` (min, then max) is the
// part copied, so taps past it read its edge, not stale pixels beside it.
constexpr const char *blur_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 direction;
uniform vec4 area;
out vec4 finalColor;
vec3 tap(vec2 uv) { return texture(texture0, clamp(uv, area.xy, area.zw)).rgb; }
void main() {
  const float w[5] = float[](0.2270270270, 0.1945945946, 0.1216216216,
                             0.0540540541, 0.0162162162);
  vec3 c = tap(fragTexCoord) * w[0];
  for (int i = 1; i < 5; i++) {
    c += tap(fragTexCoord + direction * float(i)) * w[i];
    c += tap(fragTexCoord - direction * float(i)) * w[i];
  }
  finalColor = vec4(c, 1.0);
}
)";

// The blurred picture cut to a rounded rectangle, its edge anti-aliased as
// the smooth shapes' (njin_sdf2d.cpp). `uvRect` is the texture coordinate at
// the rectangle's top left, then at its bottom right.
constexpr const char *mask_fs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 shape; // x, y half the rectangle's size; z corner radius
uniform vec4 uvRect;
out vec4 finalColor;

float rounded_box(vec2 p, vec2 half_size, float r) {
  vec2 q = abs(p) - half_size + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
  vec2 t = (fragTexCoord - uvRect.xy) / (uvRect.zw - uvRect.xy);
  vec2 p = (t - 0.5) * shape.xy * 2.0;
  float d = rounded_box(p, shape.xy, shape.z);
  float aa = max(length(fwidth(p)) * 0.7071, 1e-4);
  float a = clamp(0.5 - d / aa, 0.0, 1.0);
  finalColor = vec4(texture(texture0, fragTexCoord).rgb * fragColor.rgb, fragColor.a * a);
}
)";

bool ready(backdrop_state &s) {
  if (!s.tried) {
    s.tried = true;
    s.blur = LoadShaderFromMemory(nullptr, blur_fs);
    s.mask = LoadShaderFromMemory(nullptr, mask_fs);
    if (!IsShaderValid(s.blur) || !IsShaderValid(s.mask)) {
      NJIN_WARN("draw: the backdrop blur's shaders failed to compile; draw_backdrop_blur does nothing");
      if (IsShaderValid(s.blur))
        UnloadShader(s.blur);
      if (IsShaderValid(s.mask))
        UnloadShader(s.mask);
      s.blur = s.mask = Shader{};
    } else {
      s.blur_dir = GetShaderLocation(s.blur, "direction");
      s.blur_area = GetShaderLocation(s.blur, "area");
      s.mask_shape = GetShaderLocation(s.mask, "shape");
      s.mask_uv = GetShaderLocation(s.mask, "uvRect");
    }
  }
  return s.blur.id != 0;
}

// (Re)creates `target` at `w`x`h` when its size differs.
bool ensure(RenderTexture2D &target, i32 w, i32 h) {
  if (IsRenderTextureValid(target) && target.texture.width == w && target.texture.height == h)
    return true;
  if (IsRenderTextureValid(target))
    UnloadRenderTexture(target);
  target = LoadRenderTexture(w, h);
  if (!IsRenderTextureValid(target))
    return false;
  SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
  return true;
}

// `from` (top down, in `src`'s pixels) blurred along `dir` into `to` (top
// down, in `dst`'s pixels). Render textures are stored bottom up: a negative
// source height flips.
void pass(const backdrop_state &s, const Texture2D &src, Rectangle from, RenderTexture2D &dst, Rectangle to,
          vec2 dir) {
  const f32 w = (f32)src.width, h = (f32)src.height;
  const Rectangle source{from.x, h - from.y - from.height, from.width, -from.height};
  // Half a texel in from the part's edge, so bilinear taps never mix in a pixel outside it.
  const f32 area[4] = {(source.x + 0.5f) / w, (source.y + 0.5f) / h, (source.x + from.width - 0.5f) / w,
                       (source.y + from.height - 0.5f) / h};
  const f32 direction[2] = {dir.x, dir.y};
  SetShaderValue(s.blur, s.blur_dir, direction, SHADER_UNIFORM_VEC2);
  SetShaderValue(s.blur, s.blur_area, area, SHADER_UNIFORM_VEC4);
  BeginTextureMode(dst);
  BeginShaderMode(s.blur);
  DrawTexturePro(src, source, to, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  EndShaderMode();
  EndTextureMode();
}

} // namespace

backdrop_state::~backdrop_state() {
  if (!IsWindowReady())
    return;
  for (RenderTexture2D *t : {&grab, &half_a, &half_b})
    if (IsRenderTextureValid(*t))
      UnloadRenderTexture(*t);
  if (blur.id != 0)
    UnloadShader(blur);
  if (mask.id != 0)
    UnloadShader(mask);
}

void draw_backdrop_blur(const context &ctx, rect r, f32 blur, f32 radius, rgba tint) {
  const view_state &view = ctx_ref<view_state>(ctx);
  if (blur <= 0.0f || r.size.x <= 0.0f || r.size.y <= 0.0f || tint.a <= 0.0f)
    return;
  if (view.world_depth > 0 || view.offscreen_depth > 0 || gpu_is_software())
    return;
  backdrop_state &s = ctx_mut<backdrop_state>(ctx);
  if (!ready(s))
    return;
  // The framebuffer drawn into, and where `r` is on it: through the transform
  // of the UI canvas or the smooth UI pass, then the supersampling of a virtual
  // target (its projection is at the logical size).
  const bool virtual_target = view.drawing && !view.ui_window;
  const u32 fbo = virtual_target ? view.target.id : 0;
  const i32 fw = virtual_target ? view.target.texture.width : GetRenderWidth();
  const i32 fh = virtual_target ? view.target.texture.height : GetRenderHeight();
  const f32 k = virtual_target ? (f32)view.render_scale : 1.0f;
  const Matrix m = rlGetMatrixTransform();
  const Vector3 a = Vector3Transform(Vector3{r.pos.x, r.pos.y, 0.0f}, m);
  const Vector3 b = Vector3Transform(Vector3{r.pos.x + r.size.x, r.pos.y + r.size.y, 0.0f}, m);
  const f32 x0 = std::min(a.x, b.x) * k, x1 = std::max(a.x, b.x) * k;
  const f32 y0 = std::min(a.y, b.y) * k, y1 = std::max(a.y, b.y) * k;
  // How far the blur reaches, framebuffer pixels; the part copied is `r` and
  // that much round it, inside the framebuffer.
  const f32 reach = blur * (x1 - x0) / r.size.x;
  const i32 gx0 = std::clamp((i32)std::floor(x0 - reach) - 2, 0, fw);
  const i32 gy0 = std::clamp((i32)std::floor(y0 - reach) - 2, 0, fh);
  const i32 gx1 = std::clamp((i32)std::ceil(x1 + reach) + 2, 0, fw);
  const i32 gy1 = std::clamp((i32)std::ceil(y1 + reach) + 2, 0, fh);
  if (gx1 <= gx0 || gy1 <= gy0 || fw <= 0 || fh <= 0)
    return;
  const i32 hw = (fw + 1) / 2, hh = (fh + 1) / 2;
  if (!ensure(s.grab, fw, fh) || !ensure(s.half_a, hw, hh) || !ensure(s.half_b, hw, hh))
    return;
  // Text queued to be drawn sharp later is not in the picture yet: under the
  // blur it would stay sharp, so it goes.
  view_text_occlude(view, r, rgba{0.0f, 0.0f, 0.0f, 1.0f}, true);

  // Out of the UI's transform and clip (a scissor would cut the copy and the
  // passes too), then the part copied as it is, rows counted from the bottom.
  view_ui_suspend(view);
  rlDrawRenderBatchActive();
  rlDisableScissorTest();
  rlBindFramebuffer(RL_READ_FRAMEBUFFER, fbo);
  rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, s.grab.id);
  rlBlitFramebuffer(gx0, fh - gy1, gx1, fh - gy0, gx0, fh - gy1, gx1, fh - gy0, color_bits);
  rlDisableFramebuffer();

  // Blurred at half size, across then down; a wide blur twice, with taps
  // closer together, so they do not show as bands. Steps in texture
  // coordinates are the same on the full and the half targets.
  const f32 sx = (f32)hw / (f32)fw, sy = (f32)hh / (f32)fh;
  const Rectangle full{(f32)gx0, (f32)gy0, (f32)(gx1 - gx0), (f32)(gy1 - gy0)};
  const Rectangle half{full.x * sx, full.y * sy, full.width * sx, full.height * sy};
  const i32 rounds = reach > 8.0f ? 2 : 1;
  const f32 step = std::max(reach / 4.0f / std::sqrt((f32)rounds), 0.5f);
  const vec2 across{step / (f32)fw, 0.0f}, down{0.0f, step / (f32)fh};
  pass(s, s.grab.texture, full, s.half_a, half, across);
  pass(s, s.half_a.texture, half, s.half_b, half, down);
  if (rounds > 1) {
    pass(s, s.half_b.texture, half, s.half_a, half, across);
    pass(s, s.half_a.texture, half, s.half_b, half, down);
  }

  // Back to what was drawn into, the UI's transform and clip with it.
  view_rebind(view);
  if (view.clip.size.x > 0.0f && view.clip.size.y > 0.0f)
    clip_begin(ctx, view.clip);

  // The blurred picture over `r`, cut to its corners.
  const f32 tw = (f32)hw, th = (f32)hh;
  const Rectangle part{x0 * sx, y0 * sy, (x1 - x0) * sx, (y1 - y0) * sy};
  const Rectangle source{part.x, th - part.y - part.height, part.width, -part.height};
  const f32 uv[4] = {source.x / tw, (source.y + part.height) / th, (source.x + part.width) / tw, source.y / th};
  const f32 corner = std::clamp(radius, 0.0f, 0.5f * std::min(r.size.x, r.size.y));
  const f32 shape[4] = {0.5f * r.size.x, 0.5f * r.size.y, corner, 0.0f};
  rlDrawRenderBatchActive();
  SetShaderValue(s.mask, s.mask_shape, shape, SHADER_UNIFORM_VEC4);
  SetShaderValue(s.mask, s.mask_uv, uv, SHADER_UNIFORM_VEC4);
  BeginShaderMode(s.mask);
  Color c{};
  to_raylib(tint, c);
  DrawTexturePro(s.half_b.texture, source, Rectangle{r.pos.x, r.pos.y, r.size.x, r.size.y}, Vector2{0.0f, 0.0f},
                 0.0f, c);
  EndShaderMode();
}

} // namespace njin
