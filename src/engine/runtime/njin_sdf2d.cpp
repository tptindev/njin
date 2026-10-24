#include "njin_sdf2d.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "modules/fx.h"
#include "njin_shader.h"
#include "njin_draw_shapes.h"
#include "njin_log.h"
#include <algorithm>
#include <rlgl.h>

namespace njin {
namespace {

// The quad is the shape grown by `pad` on each side, so the soft pixels just
// outside its edge are drawn too. Its texture coordinates go 0..1 over it;
// fwidth() of the position in the shape's own units gives a pixel's size in
// them, whatever the scale the quad is drawn at (a virtual resolution, the
// smooth UI pass).
constexpr f32 pad = 2.0f;

constexpr const char *sdf_fs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform vec4 sdfShape; // x, y half the shape's size; z corner radius; w line thickness (0: filled)
uniform vec2 sdfQuad;  // the quad's size
out vec4 finalColor;

float rounded_box(vec2 p, vec2 half_size, float r) {
  vec2 q = abs(p) - half_size + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
  vec2 p = (fragTexCoord - 0.5) * sdfQuad;
  float d = rounded_box(p, sdfShape.xy, sdfShape.z);
  float aa = max(length(fwidth(p)) * 0.7071, 1e-4);
  float a = clamp(0.5 - d / aa, 0.0, 1.0);
  if (sdfShape.w > 0.0)
    a *= clamp(0.5 + (d + sdfShape.w) / aa, 0.0, 1.0);
  finalColor = vec4(fragColor.rgb, fragColor.a * a);
}
)";

bool ready(sdf2d_state &s) {
  if (!s.tried) {
    s.tried = true;
    s.shader = LoadShaderFromMemory(nullptr, sdf_fs);
    if (!IsShaderValid(s.shader)) {
      NJIN_WARN("draw: the smooth shapes' shader failed to compile; shapes are drawn the usual way");
      s.shader = Shader{};
    } else {
      s.shape_loc = GetShaderLocation(s.shader, "sdfShape");
      s.quad_loc = GetShaderLocation(s.shader, "sdfQuad");
    }
  }
  return s.shader.id != 0;
}

} // namespace

sdf2d_state::~sdf2d_state() {
  if (shader.id != 0 && IsWindowReady())
    UnloadShader(shader);
}

bool sdf2d_rounded_rect(const context &ctx, rect r, f32 radius, f32 thickness, rgba color) {
  sdf2d_state &s = ctx_mut<sdf2d_state>(ctx);
  // Another shader on (shader_begin(), a dissolve) keeps its own: the shape
  // is drawn the usual way under it.
  if (!s.on || ctx_ref<shader_store>(ctx)->active.id != 0 || ctx_ref<fx_state>(ctx)->screen_dissolve)
    return false;
  if (r.size.x <= 0.0f || r.size.y <= 0.0f || color.a <= 0.0f)
    return true; // nothing to draw
  if (!ready(s))
    return false;
  const vec2 half = r.size * 0.5f;
  const f32 rad = std::clamp(radius, 0.0f, std::min(half.x, half.y));
  const f32 shape[4] = {half.x, half.y, rad, std::max(0.0f, thickness)};
  const f32 quad[2] = {r.size.x + 2.0f * pad, r.size.y + 2.0f * pad};
  // Uniforms change per shape: the batch so far is drawn with the last ones first.
  BeginShaderMode(s.shader);
  SetShaderValue(s.shader, s.shape_loc, shape, SHADER_UNIFORM_VEC4);
  SetShaderValue(s.shader, s.quad_loc, quad, SHADER_UNIFORM_VEC2);
  const Texture2D white{rlGetTextureIdDefault(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  Color c{};
  to_raylib(color, c);
  DrawTexturePro(white, Rectangle{0.0f, 0.0f, 1.0f, 1.0f},
                 Rectangle{r.pos.x - pad, r.pos.y - pad, quad[0], quad[1]}, Vector2{0.0f, 0.0f}, 0.0f, c);
  EndShaderMode();
  return true;
}

void draw_set_smooth_shapes(context &ctx, bool on) { ctx_mut<sdf2d_state>(ctx)->on = on; }

bool draw_smooth_shapes(const context &ctx) { return ctx_mut<sdf2d_state>(ctx)->on; }

} // namespace njin
