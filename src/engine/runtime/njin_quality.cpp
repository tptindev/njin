#include "njin_quality.h"
#include "njin_ctx_impl.h"
#include "njin_gpu_caps.h"
#include "njin_log.h"
#include <algorithm>
#include <rlgl.h>
#include <string>

namespace njin {
namespace {
// Lower case, as gpu_renderer_name() returns it.
bool has(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

// The number right after `prefix` in `text` (Adreno 6xx, Mali-G7x), or -1.
i32 number_after(const std::string &text, const char *prefix) {
  const usize at = text.find(prefix);
  if (at == std::string::npos)
    return -1;
  usize i = at + std::char_traits<char>::length(prefix);
  while (i < text.size() && (text[i] == ' ' || text[i] == '-'))
    i++;
  i32 n = -1;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9')
    n = (n < 0 ? 0 : n * 10) + (text[i++] - '0');
  return n;
}

// Sorts a GL renderer name (lower case) into a tier. Integrated and mobile GPUs
// are told apart by name only: there is no portable way to ask a GL driver how
// fast it is, and a guess that errs low is the safe one for a first run.
quality_level tier_of(const std::string &r) {
  if (has(r, "gma") || has(r, "videocore") || has(r, "v3d") || has(r, "powervr") ||
      has(r, "sgx") || has(r, "vivante") || has(r, "lima"))
    return quality_low;
  if (has(r, "adreno"))
    return number_after(r, "adreno") >= 600 ? quality_medium : quality_low;
  if (has(r, "mali")) {
    const i32 g = number_after(r, "mali-g");
    return g >= 70 || has(r, "immortalis") ? quality_medium : quality_low;
  }
  if (has(r, "intel")) {
    // "uhd graphics" contains "hd graphics": check the newer names first.
    if (has(r, "uhd") || has(r, "iris") || has(r, "arc"))
      return has(r, "arc(tm) a") ? quality_high : quality_medium;
    if (has(r, "hd graphics"))
      return quality_low;
    return quality_medium;
  }
  // AMD APUs: "AMD Radeon(TM) Graphics", "Radeon Vega 8 Graphics", "R7 Graphics".
  if (has(r, "radeon") && has(r, "graphics") && !has(r, " rx "))
    return quality_medium;
  return quality_high;
}
} // namespace

graphics_quality graphics_quality_preset(quality_level level) {
  graphics_quality q{};
  if (level == quality_high)
    return q;
  q.lamp_shadows = false;
  q.ssr = false;
  q.motion_blur = false;
  q.taa = false;
  if (level == quality_medium) {
    q.shadow_size_max = 1024;
    q.ssao_samples_max = 8;
    q.lighting_scale_max = 0.75f;
    q.particles = 0.75f;
    q.lod_scale = 1.5f;
    q.view_distance = 0.75f;
    return q;
  }
  q.world_scale = 0.7f;
  q.shadows = false;
  q.ssao = false;
  q.sun_shafts = false;
  q.bloom = false;
  q.dof = false;
  q.lighting_scale_max = 0.5f;
  q.particles = 0.5f;
  q.lod_scale = 2.5f;
  q.view_distance = 0.5f;
  return q;
}

void graphics_quality_set(context &ctx, const graphics_quality &quality) {
  graphics_quality q = quality;
  q.world_scale = std::clamp(q.world_scale, 0.25f, 1.0f);
  q.shadow_size_max = std::max(q.shadow_size_max, 0);
  q.lamp_shadow_size_max = std::max(q.lamp_shadow_size_max, 0);
  q.ssao_samples_max = std::max(q.ssao_samples_max, 0);
  q.ssr_steps_max = std::max(q.ssr_steps_max, 0);
  q.lighting_scale_max = std::clamp(q.lighting_scale_max, 0.25f, 1.0f);
  q.particles = std::clamp(q.particles, 0.0f, 1.0f);
  q.lod_scale = std::max(q.lod_scale, 0.0f);
  q.view_distance = std::clamp(q.view_distance, 0.1f, 1.0f);
  ctx.quality.q = q;
  ctx.quality.set = true;
  if (ctx.hooks.quality_changed != nullptr)
    ctx.hooks.quality_changed(ctx);
  post_fx_apply_quality(ctx);
}

graphics_quality graphics_quality_get(const context &ctx) { return ctx.quality.q; }

quality_level graphics_quality_suggest(const context &) {
  // ES 2.0 sorts after 3.3 in rlgl's enum, so name the old ones.
  const int gl = rlGetVersion();
  if (gpu_is_software() || gl == RL_OPENGL_SOFTWARE || gl == RL_OPENGL_11 ||
      gl == RL_OPENGL_21 || gl == RL_OPENGL_ES_20)
    return quality_low;
  const std::string renderer = gpu_renderer_name();
  const quality_level level = tier_of(renderer);
  NJIN_INFO("graphics_quality_suggest: %s -> %s", renderer.c_str(),
            level == quality_low ? "low" : level == quality_medium ? "medium" : "high");
  return level;
}

namespace {
// One table for both directions of the settings file.
struct f32_field {
  const char *name;
  f32 graphics_quality::*field;
};
struct i32_field {
  const char *name;
  i32 graphics_quality::*field;
};
struct bool_field {
  const char *name;
  bool graphics_quality::*field;
};
constexpr f32_field f32_fields[] = {
    {"world_scale", &graphics_quality::world_scale},
    {"lighting_scale_max", &graphics_quality::lighting_scale_max},
    {"particles", &graphics_quality::particles},
    {"lod_scale", &graphics_quality::lod_scale},
    {"view_distance", &graphics_quality::view_distance},
};
constexpr i32_field i32_fields[] = {
    {"shadow_size_max", &graphics_quality::shadow_size_max},
    {"lamp_shadow_size_max", &graphics_quality::lamp_shadow_size_max},
    {"ssao_samples_max", &graphics_quality::ssao_samples_max},
    {"ssr_steps_max", &graphics_quality::ssr_steps_max},
};
constexpr bool_field bool_fields[] = {
    {"shadows", &graphics_quality::shadows},     {"lamp_shadows", &graphics_quality::lamp_shadows},
    {"ssao", &graphics_quality::ssao},           {"ssr", &graphics_quality::ssr},
    {"motion_blur", &graphics_quality::motion_blur}, {"taa", &graphics_quality::taa},
    {"sun_shafts", &graphics_quality::sun_shafts}, {"bloom", &graphics_quality::bloom},
    {"dof", &graphics_quality::dof},
};
} // namespace

bool quality_was_set(const context &ctx) { return ctx.quality.set; }

json_value quality_to_json(const graphics_quality &q) {
  json_value out = json_value::make_object();
  for (const f32_field &f : f32_fields)
    out.set(f.name, q.*f.field);
  for (const i32_field &f : i32_fields)
    out.set(f.name, q.*f.field);
  for (const bool_field &f : bool_fields)
    out.set(f.name, q.*f.field);
  return out;
}

graphics_quality quality_from_json(const json_value &json, graphics_quality base) {
  for (const f32_field &f : f32_fields)
    base.*f.field = json[f.name].f32_or(base.*f.field);
  for (const i32_field &f : i32_fields)
    base.*f.field = (i32)json[f.name].f32_or((f32)(base.*f.field));
  for (const bool_field &f : bool_fields)
    base.*f.field = json[f.name].bool_or(base.*f.field);
  return base;
}

f32 quality_world_scale(const context &ctx) {
  if (view_has_virtual_size(ctx.view))
    return 1.0f;
  return std::clamp(ctx.quality.q.world_scale, 0.25f, 1.0f);
}
} // namespace njin
