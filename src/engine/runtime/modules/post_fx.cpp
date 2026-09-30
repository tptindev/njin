#include "post_fx.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include <algorithm>
#include <rlgl.h>

namespace njin {
namespace {
// A blur at least this wide (screen pixels) runs at half size.
constexpr f32 half_blur_min = 3.0f;

// Keeps only what is brighter than `threshold`, fading in over a short knee
// so the bloom does not switch on with a hard edge.
constexpr const char *bright_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform float threshold;
out vec4 finalColor;
void main() {
  vec3 c = texture(texture0, fragTexCoord).rgb;
  float l = max(c.r, max(c.g, c.b));
  float k = smoothstep(threshold, threshold + 0.2, l);
  finalColor = vec4(c * k, 1.0);
}
)";

// One direction of a 9-tap Gaussian. `direction` is the step between taps in
// texture coordinates.
constexpr const char *blur_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 direction;
out vec4 finalColor;
void main() {
  const float w[5] = float[](0.2270270270, 0.1945945946, 0.1216216216,
                             0.0540540541, 0.0162162162);
  vec3 c = texture(texture0, fragTexCoord).rgb * w[0];
  for (int i = 1; i < 5; i++) {
    c += texture(texture0, fragTexCoord + direction * float(i)).rgb * w[i];
    c += texture(texture0, fragTexCoord - direction * float(i)).rgb * w[i];
  }
  finalColor = vec4(c, 1.0);
}
)";

// Depth of field: the sharp image (texture0) and a blurred copy (blurTex),
// mixed by how far each pixel's depth is from the focus. The depth texture
// holds window depth; turned back into distance along the view.
constexpr const char *dof_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D blurTex;
uniform sampler2D depthTex;
uniform vec2 planes; // near, far
uniform vec3 focus;  // distance, sharp range, falloff
out vec4 finalColor;
void main() {
  vec3 sharp = texture(texture0, fragTexCoord).rgb;
  vec3 soft = texture(blurTex, fragTexCoord).rgb;
  float d = texture(depthTex, fragTexCoord).r * 2.0 - 1.0;
  float n = planes.x, f = planes.y;
  float z = 2.0 * n * f / (f + n - d * (f - n));
  float k = smoothstep(focus.y, focus.y + max(focus.z, 1e-4), abs(z - focus.x));
  finalColor = vec4(mix(sharp, soft, k), 1.0);
}
)";

constexpr const char *uber_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D bloomTex;
uniform float bloom;
uniform vec2 resolution;
uniform float time;
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float sepia;
uniform vec4 tint;
uniform float vignette;
uniform float vignetteRadius;
uniform float vignetteSoftness;
uniform vec4 vignetteColor;
uniform float chromatic;
uniform float scanlines;
uniform float scanlineSize;
uniform float curve;
uniform float pixelate;
uniform float grain;
out vec4 finalColor;

float hash(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
  vec2 uv = fragTexCoord;
  if (curve > 0.0) {
    vec2 cc = uv * 2.0 - 1.0;
    cc *= 1.0 + curve * (cc.yx * cc.yx);
    uv = cc * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
      finalColor = vec4(0.0, 0.0, 0.0, 1.0);
      return;
    }
  }
  if (pixelate >= 2.0) {
    vec2 cell = pixelate / resolution;
    uv = (floor(uv / cell) + 0.5) * cell;
  }

  vec3 col;
  if (chromatic > 0.0) {
    vec2 shift = (uv - 0.5) * 2.0 * chromatic / resolution;
    col.r = texture(texture0, uv + shift).r;
    col.g = texture(texture0, uv).g;
    col.b = texture(texture0, uv - shift).b;
  } else {
    col = texture(texture0, uv).rgb;
  }
  if (bloom > 0.0)
    col += texture(bloomTex, uv).rgb * bloom;

  col += brightness;
  col = (col - 0.5) * contrast + 0.5;
  float l = dot(col, vec3(0.299, 0.587, 0.114));
  col = mix(vec3(l), col, saturation);
  if (sepia > 0.0) {
    vec3 s = vec3(dot(col, vec3(0.393, 0.769, 0.189)),
                  dot(col, vec3(0.349, 0.686, 0.168)),
                  dot(col, vec3(0.272, 0.534, 0.131)));
    col = mix(col, s, sepia);
  }
  col *= tint.rgb;

  if (scanlines > 0.0) {
    float s = 0.5 + 0.5 * cos(fragTexCoord.y * resolution.y * 6.2831853 / scanlineSize);
    col *= 1.0 - scanlines * s;
  }
  if (vignette > 0.0) {
    vec2 d = (uv - 0.5) * vec2(resolution.x / resolution.y, 1.0);
    float r = length(d) / length(vec2(resolution.x / resolution.y, 1.0) * 0.5);
    float v = smoothstep(vignetteRadius, vignetteRadius + vignetteSoftness, r);
    col = mix(col, vignetteColor.rgb, v * vignette);
  }
  if (grain > 0.0)
    col += (hash(fragTexCoord * resolution + fract(time) * 100.0) - 0.5) * grain;

  finalColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)";

bool effects_in_uber(const post_fx &p) {
  return p.brightness != 0.0f || p.contrast != 1.0f || p.saturation != 1.0f ||
         p.sepia > 0.0f || p.tint.r != 1.0f || p.tint.g != 1.0f ||
         p.tint.b != 1.0f || p.vignette > 0.0f || p.bloom > 0.0f ||
         p.chromatic > 0.0f || p.scanlines > 0.0f || p.crt_curve > 0.0f ||
         p.pixelate >= 2.0f || p.grain > 0.0f;
}

bool load(post_chain &c) {
  if (c.loaded || c.failed)
    return c.loaded;
  c.bright = LoadShaderFromMemory(nullptr, bright_fs);
  c.blur = LoadShaderFromMemory(nullptr, blur_fs);
  c.uber = LoadShaderFromMemory(nullptr, uber_fs);
  c.dof = LoadShaderFromMemory(nullptr, dof_fs);
  if (!IsShaderValid(c.bright) || !IsShaderValid(c.blur) || !IsShaderValid(c.uber) || !IsShaderValid(c.dof)) {
    NJIN_WARN("post_fx: built-in shaders failed to compile; effects disabled");
    c.failed = true;
    return false;
  }
  c.bright_threshold = GetShaderLocation(c.bright, "threshold");
  c.blur_direction = GetShaderLocation(c.blur, "direction");
  c.dof_blur = GetShaderLocation(c.dof, "blurTex");
  c.dof_depth = GetShaderLocation(c.dof, "depthTex");
  c.dof_planes = GetShaderLocation(c.dof, "planes");
  c.dof_focus = GetShaderLocation(c.dof, "focus");
  const Shader &u = c.uber;
  c.u_bloom_tex = GetShaderLocation(u, "bloomTex");
  c.u_bloom = GetShaderLocation(u, "bloom");
  c.u_resolution = GetShaderLocation(u, "resolution");
  c.u_time = GetShaderLocation(u, "time");
  c.u_brightness = GetShaderLocation(u, "brightness");
  c.u_contrast = GetShaderLocation(u, "contrast");
  c.u_saturation = GetShaderLocation(u, "saturation");
  c.u_sepia = GetShaderLocation(u, "sepia");
  c.u_tint = GetShaderLocation(u, "tint");
  c.u_vignette = GetShaderLocation(u, "vignette");
  c.u_vignette_radius = GetShaderLocation(u, "vignetteRadius");
  c.u_vignette_softness = GetShaderLocation(u, "vignetteSoftness");
  c.u_vignette_color = GetShaderLocation(u, "vignetteColor");
  c.u_chromatic = GetShaderLocation(u, "chromatic");
  c.u_scanlines = GetShaderLocation(u, "scanlines");
  c.u_scanline_size = GetShaderLocation(u, "scanlineSize");
  c.u_curve = GetShaderLocation(u, "curve");
  c.u_pixelate = GetShaderLocation(u, "pixelate");
  c.u_grain = GetShaderLocation(u, "grain");
  c.loaded = true;
  return true;
}

// (Re)creates `target` at `w`x`h` when its size differs.
bool ensure(RenderTexture2D &target, i32 w, i32 h) {
  if (IsRenderTextureValid(target) && target.texture.width == w &&
      target.texture.height == h)
    return true;
  if (IsRenderTextureValid(target))
    UnloadRenderTexture(target);
  target = LoadRenderTexture(w, h);
  if (!IsRenderTextureValid(target))
    return false;
  SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);
  // Blur taps past an edge read the edge, not the opposite side.
  SetTextureWrap(target.texture, TEXTURE_WRAP_CLAMP);
  return true;
}

// Draws `src` over the whole of `dst`, through `shader` when given.
void blit(const Texture2D &src, RenderTexture2D &dst, const Shader *shader) {
  BeginTextureMode(dst);
  ClearBackground(BLANK);
  if (shader != nullptr)
    BeginShaderMode(*shader);
  // Render textures are stored bottom-up: a negative source height flips.
  const Rectangle source{0.0f, 0.0f, (f32)src.width, -(f32)src.height};
  const Rectangle dest{0.0f, 0.0f, (f32)dst.texture.width, (f32)dst.texture.height};
  DrawTexturePro(src, source, dest, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  if (shader != nullptr)
    EndShaderMode();
  EndTextureMode();
}

void blur_pass(post_chain &c, const Texture2D &src, RenderTexture2D &dst,
               vec2 direction) {
  const f32 value[2] = {direction.x, direction.y};
  SetShaderValue(c.blur, c.blur_direction, value, SHADER_UNIFORM_VEC2);
  blit(src, dst, &c.blur);
}

void set_f(const Shader &s, i32 loc, f32 v) {
  SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT);
}

// Writes uniforms of the uber program, which must be enabled, skipping the
// ones whose value is what the program already holds.
struct uniform_writer {
  post_chain &chain;

  void put(i32 loc, const f32 *value, i32 count, i32 type) {
    if (loc < 0)
      return;
    post_chain::uniform_memory &m = chain.uniforms[(usize)loc % chain.uniforms.size()];
    if (m.loc == loc && m.count == count && std::equal(value, value + count, m.value))
      return;
    m.loc = loc;
    m.count = count;
    std::copy(value, value + count, m.value);
    rlSetUniform(loc, value, type, 1);
  }
  void f(i32 loc, f32 v) { put(loc, &v, 1, RL_SHADER_UNIFORM_FLOAT); }
  void xy(i32 loc, f32 x, f32 y) {
    const f32 v[2] = {x, y};
    put(loc, v, 2, RL_SHADER_UNIFORM_VEC2);
  }
  void color(i32 loc, rgba c) {
    const f32 v[4] = {c.r, c.g, c.b, c.a};
    put(loc, v, 4, RL_SHADER_UNIFORM_VEC4);
  }
};
} // namespace

post_chain::~post_chain() {
  for (RenderTexture2D *t : {&full_a, &full_b, &half_a, &half_b}) {
    if (IsRenderTextureValid(*t))
      UnloadRenderTexture(*t);
  }
  if (loaded) {
    UnloadShader(bright);
    UnloadShader(blur);
    UnloadShader(uber);
    UnloadShader(dof);
  }
}

void post_chain_warmup(context &ctx) { load(ctx.postfx); }

bool post_chain_active(const post_chain &chain) {
  return !chain.failed &&
         (chain.settings.blur > 0.0f || chain.settings.dof > 0.0f || effects_in_uber(chain.settings));
}

const Texture2D &post_chain_run(context &ctx, const Texture2D &scene, const post_depth &depth) {
  post_chain &c = ctx.postfx;
  const post_fx &p = c.settings;
  if (!post_chain_active(c) || !load(c))
    return scene;
  const i32 w = scene.width;
  const i32 h = scene.height;
  const i32 hw = w / 2 > 0 ? w / 2 : 1;
  const i32 hh = h / 2 > 0 ? h / 2 : 1;
  if (!ensure(c.full_a, w, h) || !ensure(c.full_b, w, h))
    return scene;

  u32 passes = 0;
  const Texture2D *src = &scene;
  if (p.blur > 0.0f) {
    if (p.blur >= half_blur_min && ensure(c.half_a, hw, hh) && ensure(c.half_b, hw, hh)) {
      // A wide blur hides the lost detail, and at half size it touches a
      // quarter of the pixels: shrink, blur, scale back up. Four taps each
      // side, spread so the outermost reaches `blur` screen pixels.
      const f32 step = p.blur * 0.5f / 4.0f;
      // Bilinear at exactly half size averages each 2x2 block; a point-sampled
      // scene (pixel art) would keep one texel of four and show a grid.
      SetTextureFilter(*src, TEXTURE_FILTER_BILINEAR);
      blit(*src, c.half_a, nullptr);
      blur_pass(c, c.half_a.texture, c.half_b, {step / (f32)hw, 0.0f});
      blur_pass(c, c.half_b.texture, c.half_a, {0.0f, step / (f32)hh});
      blit(c.half_a.texture, c.full_b, nullptr);
      passes += 4;
    } else {
      const f32 step = p.blur / 4.0f;
      blur_pass(c, *src, c.full_a, {step / (f32)w, 0.0f});
      blur_pass(c, c.full_a.texture, c.full_b, {0.0f, step / (f32)h});
      passes += 2;
    }
    src = &c.full_b.texture;
  }

  if (p.dof > 0.0f && depth.texture != 0 && ensure(c.half_a, hw, hh) && ensure(c.half_b, hw, hh)) {
    // The whole image blurred at half size, as the wide blur above, then
    // mixed with the sharp one pixel by pixel.
    const f32 step = p.dof * 0.5f / 4.0f;
    SetTextureFilter(*src, TEXTURE_FILTER_BILINEAR);
    blit(*src, c.half_a, nullptr);
    blur_pass(c, c.half_a.texture, c.half_b, {step / (f32)hw, 0.0f});
    blur_pass(c, c.half_b.texture, c.half_a, {0.0f, step / (f32)hh});
    RenderTexture2D &dst = src == &c.full_a.texture ? c.full_b : c.full_a;
    const f32 planes[2] = {depth.near_plane, depth.far_plane};
    const f32 focus[3] = {p.dof_focus, std::max(p.dof_range, 0.0f), std::max(p.dof_falloff, 0.0f)};
    SetShaderValue(c.dof, c.dof_planes, planes, SHADER_UNIFORM_VEC2);
    SetShaderValue(c.dof, c.dof_focus, focus, SHADER_UNIFORM_VEC3);
    BeginTextureMode(dst);
    ClearBackground(BLANK);
    BeginShaderMode(c.dof);
    SetShaderValueTexture(c.dof, c.dof_blur, c.half_a.texture);
    rlSetUniformSampler(c.dof_depth, depth.texture);
    DrawTexturePro(*src, Rectangle{0.0f, 0.0f, (f32)w, -(f32)h}, Rectangle{0.0f, 0.0f, (f32)w, (f32)h},
                   Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
    src = &dst.texture;
    passes += 4;
  }

  const bool bloom = p.bloom > 0.0f && ensure(c.half_a, hw, hh) && ensure(c.half_b, hw, hh);
  if (bloom) {
    set_f(c.bright, c.bright_threshold, p.bloom_threshold);
    blit(*src, c.half_a, &c.bright);
    const f32 step = 1.5f * (p.bloom_radius > 0.0f ? p.bloom_radius : 1.0f);
    for (i32 i = 0; i < 2; i++) {
      blur_pass(c, c.half_a.texture, c.half_b, {step / (f32)hw, 0.0f});
      blur_pass(c, c.half_b.texture, c.half_a, {0.0f, step / (f32)hh});
    }
    passes += 5;
  }

  if (!effects_in_uber(p)) {
    ctx.stats.post_passes += passes;
    return *src;
  }
  passes++;
  ctx.stats.post_passes += passes;
  const Shader &u = c.uber;
  // The uber pass writes into whichever full target is not its source.
  RenderTexture2D &dst = src == &c.full_a.texture ? c.full_b : c.full_a;
  BeginTextureMode(dst);
  ClearBackground(BLANK);
  // One bind for all the uniforms (SetShaderValue would bind and unbind the
  // program for each), and only the ones that changed since the last frame:
  // a program keeps its uniform values.
  rlEnableShader(u.id);
  uniform_writer set{c};
  set.xy(c.u_resolution, (f32)w, (f32)h);
  set.f(c.u_time, ctx.time.elapsed);
  set.f(c.u_bloom, bloom ? p.bloom : 0.0f);
  rlSetUniformSampler(c.u_bloom_tex, (bloom ? c.half_a.texture : *src).id);
  set.f(c.u_brightness, p.brightness);
  set.f(c.u_contrast, p.contrast);
  set.f(c.u_saturation, p.saturation);
  set.f(c.u_sepia, p.sepia);
  set.color(c.u_tint, p.tint);
  set.f(c.u_vignette, p.vignette);
  set.f(c.u_vignette_radius, p.vignette_radius);
  set.f(c.u_vignette_softness, p.vignette_softness);
  set.color(c.u_vignette_color, p.vignette_color);
  set.f(c.u_chromatic, p.chromatic);
  set.f(c.u_scanlines, p.scanlines);
  set.f(c.u_scanline_size, p.scanline_size > 0.5f ? p.scanline_size : 0.5f);
  set.f(c.u_curve, p.crt_curve);
  set.f(c.u_pixelate, p.pixelate);
  set.f(c.u_grain, p.grain);
  rlDisableShader();
  BeginShaderMode(u);
  const Rectangle source{0.0f, 0.0f, (f32)w, -(f32)h};
  DrawTexturePro(*src, source, Rectangle{0.0f, 0.0f, (f32)w, (f32)h},
                 Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  EndShaderMode();
  EndTextureMode();
  return dst.texture;
}

void post_fx_set(context &ctx, const post_fx &fx) { ctx.postfx.settings = fx; }

post_fx post_fx_get(const context &ctx) { return ctx.postfx.settings; }

post_fx post_fx_lerp(const post_fx &a, const post_fx &b, f32 t) {
  const auto mix = [t](f32 x, f32 y) { return x + (y - x) * t; };
  const auto mix_c = [&](rgba x, rgba y) {
    return rgba{mix(x.r, y.r), mix(x.g, y.g), mix(x.b, y.b), mix(x.a, y.a)};
  };
  post_fx o{};
  o.brightness = mix(a.brightness, b.brightness);
  o.contrast = mix(a.contrast, b.contrast);
  o.saturation = mix(a.saturation, b.saturation);
  o.sepia = mix(a.sepia, b.sepia);
  o.tint = mix_c(a.tint, b.tint);
  o.vignette = mix(a.vignette, b.vignette);
  o.vignette_radius = mix(a.vignette_radius, b.vignette_radius);
  o.vignette_softness = mix(a.vignette_softness, b.vignette_softness);
  o.vignette_color = mix_c(a.vignette_color, b.vignette_color);
  o.bloom = mix(a.bloom, b.bloom);
  o.bloom_threshold = mix(a.bloom_threshold, b.bloom_threshold);
  o.bloom_radius = mix(a.bloom_radius, b.bloom_radius);
  o.blur = mix(a.blur, b.blur);
  o.dof = mix(a.dof, b.dof);
  o.dof_focus = mix(a.dof_focus, b.dof_focus);
  o.dof_range = mix(a.dof_range, b.dof_range);
  o.dof_falloff = mix(a.dof_falloff, b.dof_falloff);
  o.chromatic = mix(a.chromatic, b.chromatic);
  o.scanlines = mix(a.scanlines, b.scanlines);
  o.scanline_size = mix(a.scanline_size, b.scanline_size);
  o.crt_curve = mix(a.crt_curve, b.crt_curve);
  o.pixelate = mix(a.pixelate, b.pixelate);
  o.grain = mix(a.grain, b.grain);
  return o;
}
} // namespace njin
