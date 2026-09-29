#include "fx.h"
#include "_comps.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_draw.h"
#include "njin_log.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace njin {
namespace {
constexpr const char *flash_fs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 flashColor;
out vec4 finalColor;
void main() {
  vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
  finalColor = vec4(mix(c.rgb, flashColor.rgb, flashColor.a), c.a);
}
)";

// A sprite is cut into square patches of `grain` texels. Each patch gets a
// stable random number in [0, 1) from a hash (no noise texture); it is gone once
// the sweeping threshold passes it, and glows while the threshold is within
// `edge` of it. The threshold runs from -edge to 1 so that progress 0 shows
// everything with no glow, and progress 1 shows nothing.
constexpr const char *dissolve_fs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec4 flashColor;     // rgb, a = how much of it (0: no hit flash)
uniform vec4 dissolveEdge;   // rgb, a = how strongly the edge colour shows
uniform vec4 dissolveParams; // x progress 0..1, y grain (texels), z seed, w edge width
out vec4 finalColor;

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

void main() {
  vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
  vec2 cell = floor(fragTexCoord * vec2(textureSize(texture0, 0)) / max(dissolveParams.y, 1.0));
  float n = hash12(cell + dissolveParams.z);
  float w = dissolveParams.w;
  float d = n - (dissolveParams.x * (1.0 + w) - w);
  vec3 rgb = mix(c.rgb, flashColor.rgb, flashColor.a);
  rgb = mix(rgb, dissolveEdge.rgb, (1.0 - step(w, d)) * dissolveEdge.a);
  finalColor = vec4(rgb, c.a * step(0.0001, d));
}
)";

// Smooth pseudo-random signal in about -1..1: a few incommensurate sines,
// cheaper than noise and good enough for a shake nobody watches frame by frame.
f32 wobble(f32 t, f32 seed) {
  return 0.5f * std::sin(t * 1.00f + seed) + 0.3f * std::sin(t * 2.17f + seed * 1.3f) +
         0.2f * std::sin(t * 4.31f + seed * 2.1f);
}

// Fraction of the flash still showing.
f32 flash_amount(const flash_fx &flash) {
  if (flash.duration <= 0.0f)
    return 0.0f;
  const f32 left = 1.0f - clamp(flash.time / flash.duration, 0.0f, 1.0f);
  return flash.color.a * (flash.fade ? left : (left > 0.0f ? 1.0f : 0.0f));
}

// How far the dissolve has gone, 0 (all there) to 1 (all gone).
f32 dissolve_progress(const dissolve_fx &dissolve) {
  const f32 t = dissolve.duration <= 0.0f ? 1.0f : clamp(dissolve.time / dissolve.duration, 0.0f, 1.0f);
  return dissolve.reverse ? 1.0f - t : t;
}
} // namespace

fx_state::~fx_state() {
  if (flash_loaded && IsShaderValid(flash_shader))
    UnloadShader(flash_shader);
  if (flash_loaded && IsShaderValid(dissolve_shader))
    UnloadShader(dissolve_shader);
}

void camera_shake(njin_ctx &ctx, f32 trauma) {
  ctx.fx.trauma = clamp(ctx.fx.trauma + trauma, 0.0f, 1.0f);
}

void camera_shake_config(njin_ctx &ctx, const shake_config &config) {
  ctx.fx.shake = config;
}

f32 camera_shake_amount(const njin_ctx &ctx) { return ctx.fx.trauma; }

void hitstop(njin_ctx &ctx, f32 seconds) {
  if (seconds > ctx.fx.hitstop)
    ctx.fx.hitstop = seconds;
}

bool hitstop_active(const njin_ctx &ctx) { return ctx.fx.hitstop > 0.0f; }

void screen_flash(njin_ctx &ctx, rgba color, f32 duration) {
  ctx.fx.flash_color = color;
  ctx.fx.flash_duration = duration;
  ctx.fx.flash_time = 0.0f;
}

void sprite_flash(njin_ctx &ctx, entt::entity entity, rgba color, f32 duration) {
  entt::registry &registry = world(ctx);
  if (!registry.valid(entity))
    return;
  registry.emplace_or_replace<flash_fx>(entity,
                                        flash_fx{.color = color, .duration = duration});
}

void sprite_dissolve(njin_ctx &ctx, entt::entity entity, f32 duration, rgba edge_color) {
  entt::registry &registry = world(ctx);
  if (!registry.valid(entity))
    return;
  registry.emplace_or_replace<dissolve_fx>(entity, dissolve_fx{.edge_color = edge_color, .duration = duration});
}

void fx_frame_begin(njin_ctx &ctx) {
  fx_state &fx = ctx.fx;
  time_state &time = ctx.time;
  const f32 real = time.dt_real;
  if (fx.hitstop > 0.0f) {
    fx.hitstop -= real;
    time.dt = 0.0f;
  }
  fx.shake_time += real;
  fx.trauma = fx.trauma > 0.0f ? std::max(fx.trauma - fx.shake.decay * real, 0.0f) : 0.0f;
  if (fx.flash_duration > 0.0f) {
    fx.flash_time += real;
    if (fx.flash_time >= fx.flash_duration)
      fx.flash_duration = 0.0f;
  }
}

bool fx_shake_sample(const njin_ctx &ctx, vec2 &offset, f32 &angle) {
  const fx_state &fx = ctx.fx;
  if (fx.trauma <= 0.0f)
    return false;
  const f32 power = fx.trauma * fx.trauma;
  const f32 t = fx.shake_time * fx.shake.frequency;
  offset = {fx.shake.max_offset * power * wobble(t, 1.7f), fx.shake.max_offset * power * wobble(t, 5.3f)};
  angle = fx.shake.max_angle * power * wobble(t, 9.1f);
  return true;
}

void fx_apply_shake(const njin_ctx &ctx, Camera2D &camera) {
  vec2 offset{};
  f32 angle = 0.0f;
  if (!fx_shake_sample(ctx, offset, angle))
    return;
  camera.offset.x += offset.x;
  camera.offset.y += offset.y;
  camera.rotation += angle;
}

void fx_draw_screen_flash(njin_ctx &ctx) {
  const fx_state &fx = ctx.fx;
  if (fx.flash_duration <= 0.0f)
    return;
  rgba color = fx.flash_color;
  color.a *= 1.0f - clamp(fx.flash_time / fx.flash_duration, 0.0f, 1.0f);
  if (color.a > 0.0f)
    draw_rect(ctx, rect{{0.0f, 0.0f}, screen_size(ctx)}, color);
}

void fx_update_sprite_flashes(njin_ctx &ctx) {
  const f32 dt = delta(ctx);
  entt::registry &registry = world(ctx);
  std::vector<entt::entity> done;
  for (auto [entity, flash] : registry.view<flash_fx>().each()) {
    flash.time += dt;
    if (flash.time >= flash.duration)
      done.push_back(entity);
  }
  registry.remove<flash_fx>(done.begin(), done.end());
}

void fx_update_sprite_dissolves(njin_ctx &ctx) {
  const f32 dt = delta(ctx);
  entt::registry &registry = world(ctx);
  std::vector<entt::entity> appeared; // reverse dissolves that finished: back to a plain sprite
  std::vector<entt::entity> gone;     // dissolves that finished and want the entity destroyed
  for (auto [entity, dissolve] : registry.view<dissolve_fx>().each()) {
    dissolve.time = std::min(dissolve.time + dt, std::max(dissolve.duration, 0.0f));
    if (dissolve.time < dissolve.duration)
      continue;
    if (dissolve.reverse)
      appeared.push_back(entity);
    else if (dissolve.destroy_when_done)
      gone.push_back(entity);
  }
  registry.remove<dissolve_fx>(appeared.begin(), appeared.end());
  for (const entt::entity entity : gone)
    if (registry.valid(entity))
      registry.destroy(entity);
}

void fx_warmup(njin_ctx &ctx) {
  fx_state &fx = ctx.fx;
  if (fx.flash_loaded)
    return;
  fx.flash_loaded = true;
  fx.flash_shader = LoadShaderFromMemory(nullptr, flash_fs);
  if (!IsShaderValid(fx.flash_shader))
    NJIN_WARN("fx: sprite flash shader failed to compile");
  else
    fx.flash_color_loc = GetShaderLocation(fx.flash_shader, "flashColor");
  fx.dissolve_shader = LoadShaderFromMemory(nullptr, dissolve_fs);
  if (!IsShaderValid(fx.dissolve_shader)) {
    NJIN_WARN("fx: sprite dissolve shader failed to compile");
  } else {
    fx.dissolve_flash_loc = GetShaderLocation(fx.dissolve_shader, "flashColor");
    fx.dissolve_edge_loc = GetShaderLocation(fx.dissolve_shader, "dissolveEdge");
    fx.dissolve_params_loc = GetShaderLocation(fx.dissolve_shader, "dissolveParams");
  }
}

bool fx_flash_begin(njin_ctx &ctx, const flash_fx &flash) {
  fx_state &fx = ctx.fx;
  const f32 amount = flash_amount(flash);
  if (amount <= 0.0f)
    return false;
  fx_warmup(ctx);
  if (!IsShaderValid(fx.flash_shader))
    return false;
  const f32 value[4] = {flash.color.r, flash.color.g, flash.color.b, amount};
  BeginShaderMode(fx.flash_shader);
  SetShaderValue(fx.flash_shader, fx.flash_color_loc, value, SHADER_UNIFORM_VEC4);
  return true;
}

bool fx_dissolve_hidden(const dissolve_fx &dissolve) { return dissolve_progress(dissolve) >= 1.0f; }

bool fx_dissolve_begin(njin_ctx &ctx, const dissolve_fx &dissolve, const flash_fx *flash) {
  fx_state &fx = ctx.fx;
  fx_warmup(ctx);
  if (!IsShaderValid(fx.dissolve_shader))
    return false;
  const f32 amount = flash != nullptr ? flash_amount(*flash) : 0.0f;
  const f32 flash_value[4] = {flash != nullptr ? flash->color.r : 0.0f, flash != nullptr ? flash->color.g : 0.0f,
                              flash != nullptr ? flash->color.b : 0.0f, amount};
  const f32 edge_value[4] = {dissolve.edge_color.r, dissolve.edge_color.g, dissolve.edge_color.b,
                             dissolve.edge_color.a};
  const f32 params[4] = {dissolve_progress(dissolve), dissolve.grain, dissolve.seed,
                         std::max(dissolve.edge_width, 0.0f)};
  BeginShaderMode(fx.dissolve_shader);
  SetShaderValue(fx.dissolve_shader, fx.dissolve_flash_loc, flash_value, SHADER_UNIFORM_VEC4);
  SetShaderValue(fx.dissolve_shader, fx.dissolve_edge_loc, edge_value, SHADER_UNIFORM_VEC4);
  SetShaderValue(fx.dissolve_shader, fx.dissolve_params_loc, params, SHADER_UNIFORM_VEC4);
  return true;
}

void fx_sprite_shader_end() { EndShaderMode(); }
} // namespace njin
