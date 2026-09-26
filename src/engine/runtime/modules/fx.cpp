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
} // namespace

fx_state::~fx_state() {
  if (flash_loaded && IsShaderValid(flash_shader))
    UnloadShader(flash_shader);
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

void fx_apply_shake(const njin_ctx &ctx, Camera2D &camera) {
  const fx_state &fx = ctx.fx;
  if (fx.trauma <= 0.0f)
    return;
  const f32 power = fx.trauma * fx.trauma;
  const f32 t = fx.shake_time * fx.shake.frequency;
  camera.offset.x += fx.shake.max_offset * power * wobble(t, 1.7f);
  camera.offset.y += fx.shake.max_offset * power * wobble(t, 5.3f);
  camera.rotation += fx.shake.max_angle * power * wobble(t, 9.1f);
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

void fx_flash_end() { EndShaderMode(); }
} // namespace njin
