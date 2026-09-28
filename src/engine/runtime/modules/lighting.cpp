#include "lighting.h"
#include "lighting_internal.h"
#include "_comps.h"
#include "njin_camera.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_view.h"
#include <algorithm>
#include <cmath>
#include <raylib.h>
#include <rlgl.h>
#include <unordered_set>
#include <vector>

namespace njin {
using namespace light_impl;
namespace {
constexpr i32 buckets = 32;       // sectors (or strips) each light's edges are sorted into
constexpr i32 bucket_cap = 64;    // edges kept per bucket: the nearest when there are more
constexpr i32 bucket_rows = buckets * 64; // rows of the data texture: a light takes `buckets`
constexpr usize max_lights = 64;   // drawn per frame

void find_locations(lighting_state &s);

// The texture the edges of the buckets are uploaded to each frame, made once:
// 64 texels wide (an edge each), a row per bucket, `buckets` rows per light.
bool ensure_edge_texture(lighting_state &s) {
  if (s.edge_texture.id != 0)
    return true;
  s.edge_texture.id = rlLoadTexture(nullptr, bucket_cap, bucket_rows, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
  if (s.edge_texture.id == 0) {
    NJIN_WARN("lighting: could not make the shadow edge texture, shadows are off");
    return false;
  }
  s.edge_texture.width = bucket_cap, s.edge_texture.height = bucket_rows, s.edge_texture.mipmaps = 1;
  s.edge_texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
  SetTextureFilter(s.edge_texture, TEXTURE_FILTER_POINT);
  return true;
}

bool load(lighting_state &s) {
  if (s.loaded)
    return true;
  if (s.failed)
    return false;
  s.light = LoadShaderFromMemory(light_vs, light_fs);
  s.ambient = LoadShaderFromMemory(nullptr, ambient_fs);
  s.final_pass = LoadShaderFromMemory(nullptr, final_fs);
  s.march = LoadShaderFromMemory(nullptr, march_fs);
  if (!IsShaderValid(s.light) || !IsShaderValid(s.ambient) || !IsShaderValid(s.final_pass) || !IsShaderValid(s.march)) {
    NJIN_WARN("lighting: the light shaders did not compile, lighting is off");
    UnloadShader(s.light);
    UnloadShader(s.ambient);
    UnloadShader(s.final_pass);
    UnloadShader(s.march);
    s.failed = true;
    return false;
  }
  s.loaded = true;
  find_locations(s);
  return true;
}

// The box a cone fills: the apex, the two ends of the arc, and the arc's extreme
// points that fall inside the cone. Much smaller than the square around the
// full circle, so a narrow flashlight fills a fraction of the pixels.
rect cone_area(vec2 apex, f32 radius, f32 angle, f32 half_angle) {
  f32 min_x = apex.x, max_x = apex.x, min_y = apex.y, max_y = apex.y;
  const auto add = [&](f32 a) {
    const f32 x = apex.x + std::cos(a) * radius, y = apex.y + std::sin(a) * radius;
    min_x = std::min(min_x, x), max_x = std::max(max_x, x);
    min_y = std::min(min_y, y), max_y = std::max(max_y, y);
  };
  add(angle - half_angle);
  add(angle + half_angle);
  for (i32 k = 0; k < 4; k++) { // right, down, left, up
    const f32 axis = (f32)k * (PI * 0.5f);
    const f32 diff = std::fmod(axis - angle + 3.0f * PI, 2.0f * PI) - PI; // -PI..PI
    if (std::abs(diff) <= half_angle)
      add(axis);
  }
  return {{min_x - 1.0f, min_y - 1.0f}, {max_x - min_x + 2.0f, max_y - min_y + 2.0f}};
}

// Occluder edges bucketed by cell, so a light reads the few cells it covers
// instead of every edge in view.
struct edge_grid {
  f32 cell_size = 128.0f;
  f32 origin_x = 0.0f, origin_y = 0.0f;
  i32 nx = 1, ny = 1;
  std::vector<std::vector<u32>> cells; // kept between frames: the vectors keep their capacity
  std::vector<u32> stamp;              // per edge: the query that last returned it
  u32 query_id = 0;

  static i32 clamp_cell(f32 v, i32 n) { return std::clamp((i32)std::floor(v), 0, n - 1); }

  void build(const std::vector<occluder_edge> &edges, const rect &bounds) {
    const f32 longest = std::max(bounds.size.x, bounds.size.y);
    cell_size = std::max(128.0f, longest / 48.0f);
    origin_x = bounds.pos.x, origin_y = bounds.pos.y;
    nx = std::clamp((i32)std::ceil(bounds.size.x / cell_size), 1, 64);
    ny = std::clamp((i32)std::ceil(bounds.size.y / cell_size), 1, 64);
    if (cells.size() < (usize)(nx * ny))
      cells.resize((usize)(nx * ny));
    for (usize i = 0; i < (usize)(nx * ny); i++)
      cells[i].clear();
    stamp.assign(edges.size(), 0);
    query_id = 0;
    for (u32 i = 0; i < edges.size(); i++) {
      const occluder_edge &e = edges[i];
      const i32 x0 = clamp_cell((e.min_x - origin_x) / cell_size, nx), x1 = clamp_cell((e.max_x - origin_x) / cell_size, nx);
      const i32 y0 = clamp_cell((e.min_y - origin_y) / cell_size, ny), y1 = clamp_cell((e.max_y - origin_y) / cell_size, ny);
      for (i32 y = y0; y <= y1; y++) {
        for (i32 x = x0; x <= x1; x++)
          cells[(usize)(y * nx + x)].push_back(i);
      }
    }
  }

  // Calls `f(index)` once for each edge in a cell that `area` touches.
  template <class F> void query(const rect &area, F &&f) {
    query_id++;
    const i32 x0 = clamp_cell((area.pos.x - origin_x) / cell_size, nx),
              x1 = clamp_cell((area.pos.x + area.size.x - origin_x) / cell_size, nx);
    const i32 y0 = clamp_cell((area.pos.y - origin_y) / cell_size, ny),
              y1 = clamp_cell((area.pos.y + area.size.y - origin_y) / cell_size, ny);
    for (i32 y = y0; y <= y1; y++) {
      for (i32 x = x0; x <= x1; x++) {
        for (const u32 i : cells[(usize)(y * nx + x)]) {
          if (stamp[i] == query_id)
            continue;
          stamp[i] = query_id;
          f(i);
        }
      }
    }
  }
};

// One sprite that has a normal map or a material map.
struct gbuffer_sprite {
  i32 layer;
  f32 y;
  entt::entity entity;
};

// Buffers reused every frame, so lighting allocates nothing once warm.
struct frame_scratch {
  std::vector<f32> bucket_data;   // the rows of the edge texture, filled per light
  std::vector<i32> bucket_counts; // and how many edges each bucket holds: lights x buckets
  std::vector<u32> lists[buckets];
  std::vector<pixel_light> pixel; // per light: its row of the pixel shadow map
  std::vector<light_job> lights;
  std::vector<rect> areas;
  std::vector<occluder_edge> edges;
  edge_grid grid;
  std::vector<gbuffer_sprite> sprites;
  std::vector<u32> owner_stamp;   // per edge (by owner_first): the light it was last tested for
  std::vector<u8> owner_contains; // and whether that light was inside it
};

// Sorts the edges that can shadow a light into its buckets and writes them into
// the rows of the light `index`. A point or spot light uses sectors of angle
// around it; a directional light strips across its rays. Returns how many edges
// the light has in all.
usize fill_buckets(frame_scratch &sc, const std::vector<occluder_edge> &edges, const light_job &job, const rect &view,
                   i32 index, f32 shadow_reach) {
  const light_2d &l = *job.light;
  for (auto &list : sc.lists)
    list.clear();
  const bool sun = l.kind == light_directional;
  // The light's size, as the shader sees it: a point light's radius; the sun's disc, `size` wide seen from 600
  // units away, as a sine of its half angle and as its half width where the shadows end.
  const f32 source = sun ? 0.0f : std::max(l.size, 0.0f);
  const f32 sun_spread = sun ? std::min(std::max(l.size, 0.0f) / 600.0f, 1.0f) : 0.0f;
  const f32 sun_half = sun ? std::max(l.size, 0.0f) * shadow_reach / 600.0f : 0.0f;
  const f32 dir_x = std::cos(job.angle), dir_y = std::sin(job.angle);
  const vec2 across{-dir_y, dir_x};
  f32 lo = 0.0f, inv = 0.0f;
  if (sun) {
    const vec4 bins = directional_bins(view, job.angle);
    lo = bins.z, inv = bins.w;
  }

  usize total = 0;
  sc.grid.query(job.area, [&](u32 i) {
    const occluder_edge &e = edges[i];
    if (!overlaps(job.area, e.min_x, e.min_y, e.max_x, e.max_y))
      return;
    // A ray is stopped only where it enters a solid, which happens only with some of the light behind the
    // edge: an edge that faces the whole of the light (its centre, and its size to either side) never counts.
    const f32 nx = e.b.y - e.a.y, ny = -(e.b.x - e.a.x);
    const f32 n_len = std::sqrt(nx * nx + ny * ny);
    if (sun ? (nx * -dir_x + ny * -dir_y > n_len * sun_spread)
            : (nx * (job.pos.x - e.a.x) + ny * (job.pos.y - e.a.y) > n_len * source))
      return;
    // A light inside a solid is not blocked by it.
    if (e.solid && !sun) {
      if (sc.owner_stamp[e.owner_first] != (u32)index + 1) {
        sc.owner_stamp[e.owner_first] = (u32)index + 1;
        sc.owner_contains[e.owner_first] = inside_owner(edges, e, job.pos) ? 1 : 0;
      }
      if (sc.owner_contains[e.owner_first] != 0)
        return;
    }
    i32 first, last; // the buckets the edge reaches, before wrapping
    if (sun) {
      const f32 c0 = e.a.x * across.x + e.a.y * across.y, c1 = e.b.x * across.x + e.b.y * across.y;
      // The rays from a pixel to the sun's disc drift across by up to half its width.
      first = (i32)std::floor((std::min(c0, c1) - sun_half - lo) * inv - 0.5f);
      last = (i32)std::floor((std::max(c0, c1) + sun_half - lo) * inv + 0.5f);
      first = std::max(first, 0), last = std::min(last, buckets - 1);
    } else {
      // The angles at which the light sees its two ends; the edge covers the shorter way between them.
      const f32 ta = std::atan2(e.a.y - job.pos.y, e.a.x - job.pos.x), tb = std::atan2(e.b.y - job.pos.y, e.b.x - job.pos.x);
      f32 diff = tb - ta;
      if (diff > PI)
        diff -= 2.0f * PI;
      if (diff < -PI)
        diff += 2.0f * PI;
      const f32 start = diff >= 0.0f ? ta : tb, span = std::abs(diff);
      // A pixel sees the whole of the light's disc, so its rays pass an edge at distance d from the light up to
      // asin(size / d) to the side of the light's centre: the edge belongs to the sectors that far around it too.
      f32 margin = 0.06f;
      if (source > 0.0f) {
        const f32 ex = e.b.x - e.a.x, ey = e.b.y - e.a.y, len2 = std::max(ex * ex + ey * ey, 1e-6f);
        const f32 t = std::clamp(((job.pos.x - e.a.x) * ex + (job.pos.y - e.a.y) * ey) / len2, 0.0f, 1.0f);
        const f32 dx = e.a.x + ex * t - job.pos.x, dy = e.a.y + ey * t - job.pos.y;
        margin += std::asin(std::min(1.0f, source / std::max(std::sqrt(dx * dx + dy * dy), 1e-3f)));
      }
      const f32 per = (f32)buckets / (2.0f * PI);
      first = (i32)std::floor((start - margin + PI) * per);
      last = (i32)std::floor((start + span + margin + PI) * per);
      if (last - first >= buckets)
        first = 0, last = buckets - 1;
    }
    for (i32 k = first; k <= last; k++)
      sc.lists[((k % buckets) + buckets) % buckets].push_back(i);
    total++;
  });

  const vec2 centre = job.area.pos + job.area.size * 0.5f;
  const vec2 from = sun ? centre : job.pos;
  for (i32 k = 0; k < buckets; k++) {
    std::vector<u32> &list = sc.lists[k];
    if ((i32)list.size() > bucket_cap) {
      // More than fit: the ones nearest the light (or the view, for the sun).
      const auto dist = [&](u32 i) {
        const occluder_edge &e = edges[i];
        const f32 mx = (e.a.x + e.b.x) * 0.5f - from.x, my = (e.a.y + e.b.y) * 0.5f - from.y;
        return mx * mx + my * my;
      };
      std::nth_element(list.begin(), list.begin() + bucket_cap, list.end(), [&](u32 a, u32 b) { return dist(a) < dist(b); });
      list.resize((usize)bucket_cap);
    }
    const i32 row = index * buckets + k;
    sc.bucket_counts[(usize)row] = (i32)list.size();
    f32 *dst = sc.bucket_data.data() + (usize)row * (usize)bucket_cap * 4;
    for (const u32 i : list) {
      const occluder_edge &e = edges[i];
      dst[0] = e.a.x, dst[1] = e.a.y, dst[2] = e.b.x, dst[3] = e.b.y;
      dst += 4;
    }
  }
  return total;
}

// The map of a sprite that goes into a G-buffer, and the tint it is drawn with.
struct gbuffer_map {
  texture_handle map;
  Color tint = WHITE;
};

// Draws `pick(sprite)` (the map a sprite has, or an empty handle) of the
// sprites, in the order they are drawn, over `background`.
template <class Pick>
void draw_gbuffer(njin_ctx &ctx, RenderTexture2D &target, const Camera2D &camera, Color background,
                  const std::vector<gbuffer_sprite> &sprites, Pick pick) {
  entt::registry &registry = ctx.ecs.registry;
  BeginTextureMode(target);
  bind_view_target(target, screen_size(ctx));
  ClearBackground(background);
  BeginMode2D(camera);
  for (const gbuffer_sprite &g : sprites) {
    const sprite &spr = registry.get<sprite>(g.entity);
    const gbuffer_map chosen = pick(spr);
    if (chosen.map.id == 0)
      continue;
    const transform &tr = registry.get<transform>(g.entity);
    texture_store_draw_ex(ctx.texture, chosen.map,
                          texture_draw_desc{.pos = tr.pos,
                                            .source = spr.source,
                                            .scale = {tr.scale, tr.scale},
                                            .origin = spr.origin,
                                            .rotation = tr.rot,
                                            .flip_x = spr.flip_x,
                                            .flip_y = spr.flip_y,
                                            .tint = {chosen.tint.r / 255.0f, chosen.tint.g / 255.0f, chosen.tint.b / 255.0f, 1.0f}});
  }
  EndMode2D();
  EndTextureMode();
}

// Draws the normal, material and emissive maps of the sprites that have them
// into their images (a flat, default, dark surface elsewhere). Says which were needed.
void draw_gbuffers(njin_ctx &ctx, lighting_state &s, frame_scratch &sc, const Camera2D &camera, const rect &view,
                   i32 w, i32 h, bool &use_normals, bool &use_material, bool &use_emissive) {
  use_normals = use_material = use_emissive = false;
  entt::registry &registry = ctx.ecs.registry;
  std::vector<gbuffer_sprite> &found = sc.sprites;
  found.clear();
  bool any_normal = false, any_material = false, any_emissive = false;
  for (auto [entity, tr, spr] : registry.view<const transform, const sprite>().each()) {
    // Most sprites are far away: the cheapest tests first. A sprite is rarely
    // bigger than this around its anchor.
    if (!spr.visible || (spr.normal.id == 0 && spr.material.id == 0 && spr.emissive.id == 0))
      continue;
    if (!overlaps(view, tr.pos.x - 96.0f, tr.pos.y - 96.0f, tr.pos.x + 96.0f, tr.pos.y + 96.0f))
      continue;
    const bool has_normal = spr.normal.id != 0 && texture_slot_of(ctx.texture, spr.normal) != nullptr;
    const bool has_material = spr.material.id != 0 && texture_slot_of(ctx.texture, spr.material) != nullptr;
    const bool has_emissive = spr.emissive.id != 0 && texture_slot_of(ctx.texture, spr.emissive) != nullptr;
    if (!has_normal && !has_material && !has_emissive)
      continue;
    // Its real rectangle: only what touches the view is worth drawing.
    if (const texture_slot *slot = texture_slot_of(ctx.texture, spr.texture)) {
      const Rectangle area = texture_area(*slot);
      const bool whole = spr.source.size.x == 0.0f || spr.source.size.y == 0.0f;
      const f32 w = (whole ? area.width : spr.source.size.x) * std::abs(tr.scale);
      const f32 h = (whole ? area.height : spr.source.size.y) * std::abs(tr.scale);
      const f32 reach = std::hypot(std::max(spr.origin.x, 1.0f - spr.origin.x) * w, std::max(spr.origin.y, 1.0f - spr.origin.y) * h);
      if (!overlaps(view, tr.pos.x - reach, tr.pos.y - reach, tr.pos.x + reach, tr.pos.y + reach))
        continue;
    }
    any_normal = any_normal || has_normal;
    any_material = any_material || has_material;
    any_emissive = any_emissive || has_emissive;
    found.push_back({spr.layer, tr.pos.y + spr.sort_offset, entity});
  }
  if (found.empty())
    return;
  const std::unordered_set<i32> &by_y = ctx.sprites.y_sorted;
  std::stable_sort(found.begin(), found.end(), [&by_y](const gbuffer_sprite &a, const gbuffer_sprite &b) {
    if (a.layer != b.layer)
      return a.layer < b.layer;
    return by_y.contains(a.layer) && a.y < b.y;
  });
  if (any_normal && ensure_target(s.normals, w, h, true)) {
    // Flat: pointing straight at the viewer.
    draw_gbuffer(ctx, s.normals, camera, Color{128, 128, 255, 255}, found,
                 [](const sprite &p) { return gbuffer_map{p.normal, WHITE}; });
    use_normals = true;
  }
  if (any_material && ensure_target(s.materials, w, h, true)) {
    // The default surface: not metal, roughness 0.8, not occluded.
    draw_gbuffer(ctx, s.materials, camera, Color{0, 204, 255, 255}, found,
                 [](const sprite &p) { return gbuffer_map{p.material, WHITE}; });
    use_material = true;
  }
  if (any_emissive && ensure_target(s.emissives, w, h, true)) {
    // Dark: nothing glows. The power is folded into the tint (the shader multiplies by 8 in linear
    // light, and the picture is read as sRGB, hence the 1 / 2.2).
    draw_gbuffer(ctx, s.emissives, camera, BLACK, found, [](const sprite &p) {
      const f32 t = std::pow(std::clamp(p.emissive_power / 8.0f, 0.0f, 1.0f), 1.0f / 2.2f);
      const u8 v = (u8)std::lround(t * 255.0f);
      return gbuffer_map{p.emissive, Color{v, v, v, 255}};
    });
    use_emissive = true;
  }
}

void set_f(const Shader &s, i32 loc, f32 v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT); }
void set_i(const Shader &s, i32 loc, i32 v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_INT); }
void set_v2(const Shader &s, i32 loc, vec2 v) {
  const f32 d[2] = {v.x, v.y};
  SetShaderValue(s, loc, d, SHADER_UNIFORM_VEC2);
}
void set_v3(const Shader &s, i32 loc, f32 x, f32 y, f32 z) {
  const f32 d[3] = {x, y, z};
  SetShaderValue(s, loc, d, SHADER_UNIFORM_VEC3);
}

// sRGB colours in, linear light out (the usual 2.2 gamma).
f32 to_linear(f32 c) { return std::pow(std::max(c, 0.0f), 2.2f); }

// Looks every uniform up once, when the shaders load.
void find_locations(lighting_state &s) {
  light_locations &l = s.loc;
  const Shader &sh = s.light;
  l.albedo = GetShaderLocation(sh, "albedo");
  l.normals = GetShaderLocation(sh, "normals");
  l.material = GetShaderLocation(sh, "material");
  l.target_size = GetShaderLocation(sh, "target_size");
  l.use_normals = GetShaderLocation(sh, "use_normals");
  l.use_material = GetShaderLocation(sh, "use_material");
  l.kind = GetShaderLocation(sh, "kind");
  l.falloff = GetShaderLocation(sh, "falloff");
  l.lpos = GetShaderLocation(sh, "lpos");
  l.ldir = GetShaderLocation(sh, "ldir");
  l.lcolor = GetShaderLocation(sh, "lcolor");
  l.radius = GetShaderLocation(sh, "radius");
  l.source_size = GetShaderLocation(sh, "source_size");
  l.height = GetShaderLocation(sh, "height");
  l.elevation = GetShaderLocation(sh, "elevation");
  l.cone_outer = GetShaderLocation(sh, "cone_outer");
  l.cone_inner = GetShaderLocation(sh, "cone_inner");
  l.reach = GetShaderLocation(sh, "reach");
  l.edges = GetShaderLocation(sh, "edges");
  l.bucket_base = GetShaderLocation(sh, "bucket_base");
  l.bucket_count = GetShaderLocation(sh, "bucket_count");
  l.has_shadows = GetShaderLocation(sh, "has_shadows");
  l.bin_axis = GetShaderLocation(sh, "bin_axis");
  l.bin_lo = GetShaderLocation(sh, "bin_lo");
  l.bin_inv = GetShaderLocation(sh, "bin_inv");
  l.ambient_material = GetShaderLocation(s.ambient, "material");
  l.ambient_emissive = GetShaderLocation(s.ambient, "emissive_map");
  l.ambient_use_emissive = GetShaderLocation(s.ambient, "use_emissive");
  l.ambient_size = GetShaderLocation(s.ambient, "target_size");
  l.ambient_use_material = GetShaderLocation(s.ambient, "use_material");
  l.ambient_color = GetShaderLocation(s.ambient, "ambient");
  l.final_hdr = GetShaderLocation(s.final_pass, "hdr");
  l.final_size = GetShaderLocation(s.final_pass, "target_size");
  l.final_exposure = GetShaderLocation(s.final_pass, "exposure");
  l.final_tonemap = GetShaderLocation(s.final_pass, "tonemap");
  l.pixel_shadows = GetShaderLocation(sh, "pixel_shadows");
  l.pixel_row = GetShaderLocation(sh, "pixel_row");
  l.pixel_columns = GetShaderLocation(sh, "pixel_columns");
  l.pixel_inv = GetShaderLocation(sh, "pixel_inv");
  l.pixel_off = GetShaderLocation(sh, "pixel_off");
  l.pixel_tol = GetShaderLocation(sh, "pixel_tol");
  const Shader &mh = s.march;
  l.march_occluders = GetShaderLocation(mh, "occluders");
  l.march_map_size = GetShaderLocation(mh, "map_size");
  l.march_mode = GetShaderLocation(mh, "mode");
  l.march_columns = GetShaderLocation(mh, "columns");
  l.march_alpha = GetShaderLocation(mh, "alpha");
  l.march_origin = GetShaderLocation(mh, "origin");
  l.march_max_len = GetShaderLocation(mh, "max_len");
  l.march_rot = GetShaderLocation(mh, "rot");
  l.march_strip0 = GetShaderLocation(mh, "strip0");
  l.march_strip_step = GetShaderLocation(mh, "strip_step");
  l.march_dir = GetShaderLocation(mh, "dir");
  l.march_first_row = GetShaderLocation(mh, "first_row");
}

} // namespace

lighting_state::~lighting_state() {
  for (RenderTexture2D *t : {&hdr, &normals, &materials, &emissives, &lit, &occluder_map, &shadow_rows}) {
    if (IsRenderTextureValid(*t))
      UnloadRenderTexture(*t);
  }
  for (auto &[id, cpu] : images)
    UnloadImage(cpu.image);
  if (edge_texture.id != 0)
    UnloadTexture(edge_texture);
  if (loaded) {
    UnloadShader(light);
    UnloadShader(ambient);
    UnloadShader(final_pass);
    UnloadShader(march);
  }
}

bool lighting_active(const lighting_state &state) { return state.desc.enabled && !state.failed; }

const Texture2D &lighting_apply(njin_ctx &ctx, const Camera2D &camera, const Texture2D &scene) {
  lighting_state &s = ctx.light;
  if (!lighting_active(s))
    return scene;
  const lighting_desc &d = s.desc;
  static thread_local frame_scratch sc;

  // What is in view, and so which lights and occluders matter.
  const rect steady = camera_bounds(ctx);
  const rect view{steady.pos - vec2{64.0f, 64.0f}, steady.size + vec2{128.0f, 128.0f}};
  entt::registry &registry = ctx.ecs.registry;

  std::vector<light_job> &lights = sc.lights;
  lights.clear();
  f32 reach_needed = 0.0f;
  for (auto [entity, tr, l] : registry.view<const transform, const light_2d>().each()) {
    if (!l.enabled || l.intensity <= 0.0f)
      continue;
    const f32 angle = (tr.rot + l.angle) * (PI / 180.0f);
    if (l.kind == light_directional) {
      // What shadows the view can stand outside it, towards the sun.
      const f32 bx = -std::cos(angle) * d.shadow_reach, by = -std::sin(angle) * d.shadow_reach;
      const rect area{{view.pos.x + std::min(0.0f, bx), view.pos.y + std::min(0.0f, by)},
                      {view.size.x + std::abs(bx), view.size.y + std::abs(by)}};
      lights.push_back({&l, tr.pos, angle, area});
      reach_needed = std::max(reach_needed, d.shadow_reach);
      continue;
    }
    const f32 radius = std::max(l.radius, 1.0f);
    const rect area = l.kind == light_spot
                          ? cone_area(tr.pos, radius, angle, std::clamp(l.cone, 1.0f, 179.0f) * 0.5f * (PI / 180.0f))
                          : rect{tr.pos - vec2{radius, radius}, {2.0f * radius, 2.0f * radius}};
    if (!overlaps(view, area.pos.x, area.pos.y, area.pos.x + area.size.x, area.pos.y + area.size.y))
      continue;
    lights.push_back({&l, tr.pos, angle, area});
    reach_needed = std::max(reach_needed, radius);
  }
  // Too many: keep the ones nearest the middle of the view.
  if (lights.size() > max_lights) {
    const vec2 mid = view.pos + view.size * 0.5f;
    std::nth_element(lights.begin(), lights.begin() + (isize)max_lights, lights.end(),
                     [&mid](const light_job &a, const light_job &b) {
                       const f32 da = (a.pos.x - mid.x) * (a.pos.x - mid.x) + (a.pos.y - mid.y) * (a.pos.y - mid.y);
                       const f32 db = (b.pos.x - mid.x) * (b.pos.x - mid.x) + (b.pos.y - mid.y) * (b.pos.y - mid.y);
                       return da < db;
                     });
    lights.resize(max_lights);
  }
  // Nothing to add and an ambient that leaves the world as it is: nothing to do.
  const f32 exposure = std::max(d.exposure, 0.0f);
  const auto neutral = [exposure](f32 c) { return std::abs(c * exposure - 1.0f) < 0.002f; };
  if (lights.empty() && neutral(d.ambient.r) && neutral(d.ambient.g) && neutral(d.ambient.b))
    return scene;
  if (!load(s))
    return scene;

  const f32 scale = std::clamp(d.scale, 0.25f, 1.0f);
  const i32 lw = std::max(1, (i32)((f32)scene.width * scale));
  const i32 lh = std::max(1, (i32)((f32)scene.height * scale));
  const bool bilinear_scene = ctx.view.render_scale > 1;
  if (!ensure_float_target(s.hdr, lw, lh, s.hdr_ok) || !ensure_target(s.lit, scene.width, scene.height, bilinear_scene))
    return scene;

  // Occluder edges near the view, grown by the farthest a light reaches, and
  // bucketed into a grid.
  std::vector<occluder_edge> &edges = sc.edges;
  edges.clear();
  if (reach_needed > 0.0f) {
    const rect near{view.pos - vec2{reach_needed, reach_needed}, view.size + vec2{2.0f * reach_needed, 2.0f * reach_needed}};
    sc.areas.clear();
    for (const light_job &job : lights) {
      if (job.light->cast_shadows)
        sc.areas.push_back(job.area);
    }
    gather_edges(ctx, s, edges, sc.areas);
    if (!edges.empty()) {
      sc.grid.build(edges, near);
      sc.owner_stamp.assign(edges.size(), 0);
      sc.owner_contains.assign(edges.size(), 0);
    }
  }

  bool use_normals = false, use_material = false, use_emissive = false;
  draw_gbuffers(ctx, s, sc, camera, steady, lw, lh, use_normals, use_material, use_emissive);
  const light_locations &loc = s.loc;
  const rect steady_view = view;

  // Pixel-perfect shadows: the occluder map, and a row of shadow map for each light.
  sc.pixel.assign(lights.size(), pixel_light{});
  const f32 texel_scale = (f32)lw / std::max(screen_size(ctx).x, 1.0f);
  const bool have_pixel = build_pixel_shadows(ctx, s, camera, steady, texel_scale, lights, sc.pixel);

  // The light image: ambient on the albedo first, then each light adds its share.
  BeginTextureMode(s.hdr);
  ClearBackground(BLACK);
  BeginShaderMode(s.ambient);
  set_v2(s.ambient, loc.ambient_size, {(f32)lw, (f32)lh});
  set_i(s.ambient, loc.ambient_use_material, use_material ? 1 : 0);
  set_v3(s.ambient, loc.ambient_color, to_linear(d.ambient.r), to_linear(d.ambient.g), to_linear(d.ambient.b));
  set_i(s.ambient, loc.ambient_use_emissive, use_emissive ? 1 : 0);
  if (use_material)
    SetShaderValueTexture(s.ambient, loc.ambient_material, s.materials.texture);
  if (use_emissive)
    SetShaderValueTexture(s.ambient, loc.ambient_emissive, s.emissives.texture);
  DrawTexturePro(scene, Rectangle{0.0f, 0.0f, (f32)scene.width, (f32)scene.height}, Rectangle{0.0f, 0.0f, (f32)lw, (f32)lh},
                 Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  EndShaderMode();
  EndTextureMode();

  const Shader &sh = s.light;
  u32 drawn = 0;
  bool shadows_ok = false;
  if (!lights.empty() && !edges.empty() && ensure_edge_texture(s)) {
    // Every light's edges into the buckets, then to the graphics card in one go.
    const usize rows = lights.size() * (usize)buckets;
    sc.bucket_data.resize(rows * (usize)bucket_cap * 4);
    sc.bucket_counts.assign(rows, 0);
    for (usize i = 0; i < lights.size(); i++) {
      if (lights[i].light->cast_shadows)
        fill_buckets(sc, edges, lights[i], view, (i32)i, d.shadow_reach);
    }
    rlUpdateTexture(s.edge_texture.id, 0, 0, bucket_cap, (int)rows, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32,
                    sc.bucket_data.data());
    shadows_ok = true;
  }
  u32 light_index = 0;
  if (!lights.empty()) {
    BeginTextureMode(s.hdr);
    bind_view_target(s.hdr, screen_size(ctx));
    BeginMode2D(camera);
    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(sh);
    set_v2(sh, loc.target_size, {(f32)lw, (f32)lh});
    set_i(sh, loc.use_normals, use_normals ? 1 : 0);
    set_i(sh, loc.use_material, use_material ? 1 : 0);
    set_f(sh, loc.reach, d.shadow_reach);
    set_f(sh, loc.pixel_columns, (f32)s.pixel_columns_used);
    if (have_pixel) {
      // A fifth sampler, past the four raylib manages: bound here once, it stays bound across the flushes.
      const int unit = 5;
      rlActiveTextureSlot(unit);
      rlEnableTexture(s.shadow_rows.texture.id);
      rlSetUniform(loc.pixel_shadows, &unit, RL_SHADER_UNIFORM_INT, 1);
      rlActiveTextureSlot(0);
    }
    for (const light_job &job : lights) {
      const u32 this_light = light_index++;
      const light_2d &l = *job.light;
      rgba color = l.color;
      if (l.temperature > 0.0f) {
        const rgba k = light_color_kelvin(l.temperature);
        color = {color.r * k.r, color.g * k.g, color.b * k.b, 1.0f};
      }
      set_v3(sh, loc.lcolor, to_linear(color.r) * l.intensity, to_linear(color.g) * l.intensity,
             to_linear(color.b) * l.intensity);
      set_i(sh, loc.kind, (i32)l.kind);
      set_i(sh, loc.falloff, (i32)l.falloff);
      set_v2(sh, loc.lpos, job.pos);
      set_v2(sh, loc.ldir, {std::cos(job.angle), std::sin(job.angle)});
      set_f(sh, loc.radius, std::max(l.radius, 1.0f));
      set_f(sh, loc.source_size, std::max(l.size, 0.0f));
      set_f(sh, loc.height, std::max(l.height, 1.0f));
      set_f(sh, loc.elevation, std::clamp(l.elevation, 1.0f, 90.0f) * (PI / 180.0f));
      const f32 half = std::clamp(l.cone, 1.0f, 179.0f) * 0.5f * (PI / 180.0f);
      set_f(sh, loc.cone_outer, std::cos(half));
      set_f(sh, loc.cone_inner,
            std::min(std::cos(half * (1.0f - std::clamp(l.softness, 0.0f, 1.0f))), 1.0f) + 1e-3f);

      // Its pixel shadows, if it has any.
      const pixel_light &pl = sc.pixel[this_light];
      set_i(sh, loc.pixel_row, pl.row);
      if (pl.row >= 0) {
        set_f(sh, loc.pixel_inv, pl.inv);
        set_f(sh, loc.pixel_off, pl.off);
        set_f(sh, loc.pixel_tol, pl.tol);
      }
      if (l.kind == light_directional) {
        const vec4 bins = directional_bins(steady_view, job.angle);
        set_v2(sh, loc.bin_axis, {bins.x, bins.y});
        set_f(sh, loc.bin_lo, bins.z);
        set_f(sh, loc.bin_inv, bins.w);
      }

      // Its edges are in the rows the buckets were written to.
      const bool shadowed = shadows_ok && l.cast_shadows;
      set_i(sh, loc.has_shadows, shadowed ? 1 : 0);
      if (shadowed) {
        set_i(sh, loc.bucket_base, (i32)this_light * buckets);
        SetShaderValueV(sh, loc.bucket_count, sc.bucket_counts.data() + (usize)this_light * (usize)buckets, SHADER_UNIFORM_INT,
                        buckets);
        SetShaderValueTexture(sh, loc.edges, s.edge_texture);
      }
      // The samplers are forgotten at each flush: bind them again for every light.
      SetShaderValueTexture(sh, loc.albedo, scene);
      if (use_normals)
        SetShaderValueTexture(sh, loc.normals, s.normals.texture);
      if (use_material)
        SetShaderValueTexture(sh, loc.material, s.materials.texture);

      DrawRectangleRec(Rectangle{job.area.pos.x, job.area.pos.y, job.area.size.x, job.area.size.y}, WHITE);
      // Uniforms are read when the batch is drawn, so each light gets its own.
      rlDrawRenderBatchActive();
      drawn++;
    }
    EndShaderMode();
    EndBlendMode();
    EndMode2D();
    EndTextureMode();
  }

  // Exposure, tonemap and gamma: the linear light back to an ordinary image.
  BeginTextureMode(s.lit);
  BeginShaderMode(s.final_pass);
  set_v2(s.final_pass, loc.final_size, {(f32)scene.width, (f32)scene.height});
  set_f(s.final_pass, loc.final_exposure, exposure);
  set_i(s.final_pass, loc.final_tonemap, (i32)d.tonemap);
  SetShaderValueTexture(s.final_pass, loc.final_hdr, s.hdr.texture);
  DrawTexturePro(scene, Rectangle{0.0f, 0.0f, (f32)scene.width, (f32)scene.height},
                 Rectangle{0.0f, 0.0f, (f32)scene.width, (f32)scene.height}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  EndShaderMode();
  EndTextureMode();

  ctx.stats.lights += drawn;
  ctx.stats.post_passes += 3 + (use_normals ? 1u : 0u) + (use_material ? 1u : 0u) + (use_emissive ? 1u : 0u);
  return s.lit.texture;
}

void lighting_set(njin_ctx &ctx, const lighting_desc &desc) { ctx.light.desc = desc; }

lighting_desc lighting_get(const njin_ctx &ctx) { return ctx.light.desc; }

rgba light_color_kelvin(f32 kelvin) {
  const f64 t = std::clamp((f64)kelvin, 1000.0, 40000.0) / 100.0;
  f64 r, g, b;
  if (t <= 66.0) {
    r = 255.0;
    g = 99.4708025861 * std::log(t) - 161.1195681661;
  } else {
    r = 329.698727446 * std::pow(t - 60.0, -0.1332047592);
    g = 288.1221695283 * std::pow(t - 60.0, -0.0755148492);
  }
  if (t >= 66.0)
    b = 255.0;
  else if (t <= 19.0)
    b = 0.0;
  else
    b = 138.5177312231 * std::log(t - 10.0) - 305.0447927307;
  r = std::clamp(r, 0.0, 255.0) / 255.0;
  g = std::clamp(g, 0.0, 255.0) / 255.0;
  b = std::clamp(b, 0.0, 255.0) / 255.0;
  const f64 peak = std::max({r, g, b, 1e-6});
  return {(f32)(r / peak), (f32)(g / peak), (f32)(b / peak), 1.0f};
}
} // namespace njin
