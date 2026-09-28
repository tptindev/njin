#include "lighting_internal.h"
#include "_comps.h"
#include "njin_ctx_impl.h"
#include "njin_view.h"
#include <algorithm>
#include <cmath>
#include <raylib.h>
#include <rlgl.h>
#include <vector>

namespace njin::light_impl {
namespace {
constexpr i32 max_rows = 64; // lights that can have pixel shadows in one frame

// World coordinates to texels of the occluder map (y down): the world camera moved by the margin, and the
// scale from logical pixels to texels.
struct to_map {
  Camera2D camera;
  f32 sx, sy;

  vec2 operator()(vec2 world) const {
    const Vector2 p = GetWorldToScreen2D(Vector2{world.x, world.y}, camera);
    return {p.x * sx, p.y * sy};
  }
};

void set_f(const Shader &s, i32 loc, f32 v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT); }
void set_i(const Shader &s, i32 loc, i32 v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_INT); }
void set_v2(const Shader &s, i32 loc, vec2 v) {
  const f32 d[2] = {v.x, v.y};
  SetShaderValue(s, loc, d, SHADER_UNIFORM_VEC2);
}
} // namespace

bool build_pixel_shadows(njin_ctx &ctx, lighting_state &s, const Camera2D &camera, const rect &steady, f32 texel_scale,
                         const std::vector<light_job> &lights, std::vector<pixel_light> &out) {
  const lighting_desc &d = s.desc;
  entt::registry &registry = ctx.ecs.registry;
  const f32 margin_world = std::max(d.occluder_margin, 0.0f);
  const rect region{steady.pos - vec2{margin_world, margin_world}, steady.size + vec2{2.0f * margin_world, 2.0f * margin_world}};

  // Nothing to do without a light that shadows and a sprite that casts.
  bool any_light = false;
  for (const light_job &job : lights)
    any_light = any_light || job.light->cast_shadows;
  if (!any_light)
    return false;
  static thread_local std::vector<entt::entity> casters;
  casters.clear();
  for (auto [entity, tr, spr, occ] : registry.view<const transform, const sprite, const light_occluder_pixels>().each()) {
    if (!spr.visible)
      continue;
    // A sprite is rarely bigger than this around its anchor.
    if (!overlaps(region, tr.pos.x - 128.0f, tr.pos.y - 128.0f, tr.pos.x + 128.0f, tr.pos.y + 128.0f))
      continue;
    casters.push_back(entity);
  }
  if (casters.empty())
    return false;

  // The occluder map: what casts, drawn as the world is, over the view and a margin.
  const vec2 screen = screen_size(ctx);
  const f32 zoom = std::max(camera.zoom, 1e-3f);
  const i32 margin_px = (i32)std::ceil(margin_world * zoom);
  const vec2 logical{screen.x + 2.0f * (f32)margin_px, screen.y + 2.0f * (f32)margin_px};
  const i32 tw = std::max(1, (i32)std::ceil(logical.x * texel_scale)), th = std::max(1, (i32)std::ceil(logical.y * texel_scale));
  const i32 columns = std::clamp(d.shadow_columns, 128, 4096);
  if (!ensure_target(s.occluder_map, tw, th, false) || !ensure_float_target(s.shadow_rows, columns, max_rows, s.hdr_ok, true))
    return false;

  Camera2D cam2 = camera;
  cam2.offset.x += (f32)margin_px;
  cam2.offset.y += (f32)margin_px;
  const to_map map{cam2, (f32)tw / logical.x, (f32)th / logical.y};

  BeginTextureMode(s.occluder_map);
  bind_view_target(s.occluder_map, logical);
  ClearBackground(BLANK);
  BeginMode2D(cam2);
  // The alpha of the map is the sprites' own alpha, laid over one another: not multiplied by itself.
  rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
  BeginBlendMode(BLEND_CUSTOM_SEPARATE);
  for (const entt::entity e : casters) {
    const transform &tr = registry.get<transform>(e);
    const sprite &spr = registry.get<sprite>(e);
    const light_occluder_pixels &occ = registry.get<light_occluder_pixels>(e);
    const texture_handle mask = occ.mask.id != 0 ? occ.mask : spr.texture;
    if (const texture_slot *slot = texture_slot_of(ctx.texture, mask)) {
      if (s.clamped.insert(slot->texture.id).second)
        SetTextureWrap(slot->texture, TEXTURE_WRAP_CLAMP);
    }
    texture_store_draw_ex(ctx.texture, mask,
                          texture_draw_desc{.pos = tr.pos,
                                            .source = spr.source,
                                            .scale = {tr.scale, tr.scale},
                                            .origin = spr.origin,
                                            .rotation = tr.rot,
                                            .flip_x = spr.flip_x,
                                            .flip_y = spr.flip_y});
  }
  EndBlendMode();
  EndMode2D();
  EndTextureMode();

  // A row of the shadow map for each light that shadows: marched through the occluder map.
  const light_locations &loc = s.loc;
  const Shader &mh = s.march;
  const f32 rot = camera.rotation * (PI / 180.0f);
  const f32 texel_world = 1.0f / (zoom * map.sx); // world units in one texel
  const rect view{steady.pos - vec2{64.0f, 64.0f}, steady.size + vec2{128.0f, 128.0f}}; // the same as the strips of the edges
  bool any = false;
  i32 row = 0;
  BeginTextureMode(s.shadow_rows);
  ClearBackground(WHITE); // entry and exit 1: nothing
  BeginShaderMode(mh);
  set_v2(mh, loc.march_map_size, {(f32)tw, (f32)th});
  set_f(mh, loc.march_columns, (f32)columns);
  set_f(mh, loc.march_alpha, std::clamp(d.pixel_alpha, 0.01f, 1.0f));
  set_v2(mh, loc.march_rot, {std::cos(rot), std::sin(rot)});
  for (usize i = 0; i < lights.size() && row < max_rows; i++) {
    const light_job &job = lights[i];
    const light_2d &l = *job.light;
    if (!l.cast_shadows)
      continue;
    if (l.kind == light_directional) {
      // Parallel strips across the rays, from the sun's side of the map.
      const vec2 dir{std::cos(job.angle), std::sin(job.angle)}, across{-std::sin(job.angle), std::cos(job.angle)};
      const vec4 bins = directional_bins(view, job.angle);
      const f32 lo = bins.z, hi = bins.z + 32.0f / bins.w, dw = (hi - lo) / (f32)columns;
      f32 along_min = 1e30f, along_max = -1e30f;
      for (const vec2 c : {region.pos, vec2{region.pos.x + region.size.x, region.pos.y}, vec2{region.pos.x, region.pos.y + region.size.y},
                           region.pos + region.size}) {
        const f32 v = c.x * dir.x + c.y * dir.y;
        along_min = std::min(along_min, v), along_max = std::max(along_max, v);
      }
      const f32 range = std::max(along_max - along_min, 1e-3f);
      const vec2 base{across.x * (lo + 0.5f * dw) + dir.x * along_min, across.y * (lo + 0.5f * dw) + dir.y * along_min};
      const vec2 p0 = map(base);
      const vec2 p1 = map({base.x + across.x * dw, base.y + across.y * dw});
      const vec2 p2 = map({base.x + dir.x, base.y + dir.y});
      const f32 len = std::max(std::hypot(p2.x - p0.x, p2.y - p0.y), 1e-6f);
      set_i(mh, loc.march_mode, 1);
      set_v2(mh, loc.march_strip0, p0);
      set_v2(mh, loc.march_strip_step, {p1.x - p0.x, p1.y - p0.y});
      set_v2(mh, loc.march_dir, {(p2.x - p0.x) / len, (p2.y - p0.y) / len});
      set_f(mh, loc.march_max_len, range / texel_world);
      out[i] = {row, 1.0f / range, along_min, 1.5f * texel_world / range};
    } else {
      // Rays out of the light, one per column.
      const f32 radius = std::max(l.radius, 1.0f);
      set_i(mh, loc.march_mode, 0);
      set_v2(mh, loc.march_origin, map(job.pos));
      set_f(mh, loc.march_max_len, radius / texel_world);
      out[i] = {row, 1.0f / radius, 0.0f, 1.5f * texel_world / radius};
    }
    // The sampler is forgotten at each flush: bind it again for every light.
    SetShaderValueTexture(mh, loc.march_occluders, s.occluder_map.texture);
    // The target is drawn y down, so storage row `row` is drawn at the mirrored height.
    DrawRectangle(0, max_rows - 1 - row, columns, 1, WHITE);
    rlDrawRenderBatchActive();
    row++;
    any = true;
  }
  EndShaderMode();
  EndTextureMode();
  return any;
}
} // namespace njin::light_impl
