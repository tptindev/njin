#include "njin_world3d_impl.h"
#include "modules/render3d.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_draw.h"
#include "njin_ecs.h"
#include "njin_log.h"
#include "njin_path.h"
#include "njin_physics3d_impl.h"
#include "njin_render.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iterator>
#include <raylib.h>
#include <string>

namespace njin {
namespace {
constexpr f32 gravity = 9.81f;

bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// A value the game passed that is NaN or infinite: said once per function.
bool refuse(bool ok, const char *what) {
  if (ok)
    return false;
  static std::vector<const char *> told;
  if (std::find(told.begin(), told.end(), what) == told.end()) {
    told.push_back(what);
    NJIN_WARN("world3d: %s given a value that is not finite (NaN or infinite): ignored", what);
  }
  return true;
}

f32 smooth(f32 a, f32 b, f32 x) {
  if (b <= a)
    return x >= b ? 1.0f : 0.0f;
  const f32 t = clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// 1 inside [lo, hi], fading out over `soft` around each end.
f32 in_range(f32 v, f32 lo, f32 hi, f32 soft) {
  const f32 h = std::max(soft, 1e-4f) * 0.5f;
  return smooth(lo - h, lo + h, v) * (1.0f - smooth(hi - h, hi + h, v));
}

// Deterministic numbers for placement: the same seed and cell give the same
// blades and stones on every machine.
u64 splitmix(u64 x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

struct hash_rng {
  u64 s;
  explicit hash_rng(u64 seed) : s(splitmix(seed)) {}
  f32 unit() {
    s = splitmix(s);
    return (f32)(s >> 40) * (1.0f / 16777216.0f);
  }
};

template <typename T> T *slot_in(std::vector<T> &v, u32 id) {
  return id == 0 || id > v.size() || !v[id - 1].alive ? nullptr : &v[id - 1];
}
template <typename T> const T *slot_in(const std::vector<T> &v, u32 id) {
  return id == 0 || id > v.size() || !v[id - 1].alive ? nullptr : &v[id - 1];
}

std::string lower_ext(const char *path) {
  const char *dot = std::strrchr(path, '.');
  std::string ext = dot != nullptr ? dot : "";
  for (char &c : ext)
    c = (char)std::tolower((unsigned char)c);
  return ext;
}

// `w` x `h` values resampled to `res` x `res` (bilinear).
std::vector<f32> resample(const std::vector<f32> &src, i32 w, i32 h, i32 res) {
  std::vector<f32> out((usize)res * (usize)res);
  for (i32 j = 0; j < res; j++)
    for (i32 i = 0; i < res; i++) {
      const f32 fx = (f32)i * (f32)(w - 1) / (f32)(res - 1);
      const f32 fz = (f32)j * (f32)(h - 1) / (f32)(res - 1);
      const i32 x0 = std::min((i32)fx, w - 1), z0 = std::min((i32)fz, h - 1);
      const i32 x1 = std::min(x0 + 1, w - 1), z1 = std::min(z0 + 1, h - 1);
      const f32 u = fx - (f32)x0, v = fz - (f32)z0;
      const auto at = [&](i32 x, i32 z) { return src[(usize)z * (usize)w + (usize)x]; };
      out[(usize)j * (usize)res + (usize)i] =
          lerp(lerp(at(x0, z0), at(x1, z0), u), lerp(at(x0, z1), at(x1, z1), u), v);
    }
  return out;
}

// A height map as values 0..1 at `res` x `res`: an image's red channel, or a
// square 16-bit little-endian raw file (.r16, .raw).
bool load_heightmap(const char *path, i32 res, std::vector<f32> &out) {
  const std::string file = asset_path(path);
  const std::string ext = lower_ext(path);
  if (ext == ".r16" || ext == ".raw") {
    i32 size = 0;
    unsigned char *data = LoadFileData(file.c_str(), &size);
    if (data == nullptr) {
      NJIN_WARN("world3d: height map %s not found", path);
      return false;
    }
    const i32 count = size / 2;
    const i32 side = (i32)std::lround(std::sqrt((f64)count));
    if (side < 2 || side * side != count) {
      NJIN_WARN("world3d: %s is not a square 16-bit height map (%d bytes)", path, size);
      UnloadFileData(data);
      return false;
    }
    std::vector<f32> src((usize)count);
    for (i32 k = 0; k < count; k++)
      src[(usize)k] = (f32)((u32)data[2 * k] | ((u32)data[2 * k + 1] << 8)) / 65535.0f;
    UnloadFileData(data);
    out = resample(src, side, side, res);
    return true;
  }
  Image image = LoadImage(file.c_str());
  if (!IsImageValid(image)) {
    NJIN_WARN("world3d: height map %s not found or not an image", path);
    return false;
  }
  Color *pixels = LoadImageColors(image);
  std::vector<f32> src((usize)image.width * (usize)image.height);
  for (usize k = 0; k < src.size(); k++)
    src[k] = (f32)pixels[k].r / 255.0f;
  out = resample(src, image.width, image.height, res);
  UnloadImageColors(pixels);
  UnloadImage(image);
  return true;
}

bool load_splatmap(const char *path, i32 res, std::vector<u8> &out) {
  Image image = LoadImage(asset_path(path).c_str());
  if (!IsImageValid(image)) {
    NJIN_WARN("world3d: splat map %s not found or not an image", path);
    return false;
  }
  Color *pixels = LoadImageColors(image);
  out.assign((usize)res * (usize)res * 4, 0);
  std::vector<f32> channel((usize)image.width * (usize)image.height);
  for (i32 c = 0; c < 4; c++) {
    for (usize k = 0; k < channel.size(); k++) {
      const Color p = pixels[k];
      channel[k] = (f32)(c == 0 ? p.r : c == 1 ? p.g : c == 2 ? p.b : p.a);
    }
    const std::vector<f32> r = resample(channel, image.width, image.height, res);
    for (usize k = 0; k < r.size(); k++)
      out[k * 4 + (usize)c] = (u8)clamp(std::round(r[k]), 0.0f, 255.0f);
  }
  UnloadImageColors(pixels);
  UnloadImage(image);
  return true;
}

f32 sample_h(const terrain3d_slot &t, i32 i, i32 j) {
  i = std::clamp(i, 0, t.res - 1);
  j = std::clamp(j, 0, t.res - 1);
  return t.heights[(usize)j * (usize)t.res + (usize)i];
}

// Weights summing to 255, from any four.
void normalize_weights(f32 w[4], u8 *out) {
  const f32 sum = w[0] + w[1] + w[2] + w[3];
  if (sum <= 1e-6f) {
    out[0] = 255;
    out[1] = out[2] = out[3] = 0;
    return;
  }
  i32 total = 0;
  for (i32 c = 1; c < 4; c++) {
    out[c] = (u8)std::lround(w[c] / sum * 255.0f);
    total += out[c];
  }
  out[0] = (u8)std::max(0, 255 - total);
}

// The layer weights the rules give samples [x0, x1) x [z0, z1).
void rule_weights(terrain3d_slot &t, i32 x0, i32 z0, i32 x1, i32 z1) {
  const i32 n = t.desc.layer_count;
  for (i32 j = z0; j < z1; j++)
    for (i32 i = x0; i < x1; i++) {
      const f32 h = sample_h(t, i, j);
      const f32 slope = std::acos(clamp(terrain_sample_normal(t, i, j).y, -1.0f, 1.0f)) * (180.0f / pi);
      f32 w[4] = {1.0f, 0.0f, 0.0f, 0.0f};
      for (i32 l = 1; l < n; l++) {
        const terrain3d_layer &L = t.desc.layers[l];
        const f32 c = in_range(h, L.min_height, L.max_height, L.blend) *
                      in_range(slope, L.min_slope, L.max_slope, L.blend);
        for (f32 &x : w)
          x *= 1.0f - c;
        w[l] += c;
      }
      // Layer 0's own rule only thins it where a later layer has nothing to say.
      normalize_weights(w, &t.base[((usize)j * (usize)t.res + (usize)i) * 4]);
    }
}

// What is drawn: the base weights under what was painted.
void mix_splat(terrain3d_slot &t, i32 x0, i32 z0, i32 x1, i32 z1) {
  for (i32 j = z0; j < z1; j++)
    for (i32 i = x0; i < x1; i++) {
      const usize k = ((usize)j * (usize)t.res + (usize)i) * 4;
      const u8 *b = &t.base[k];
      const u8 *p = &t.paint[k];
      const f32 painted = std::min(1.0f, ((f32)p[0] + (f32)p[1] + (f32)p[2] + (f32)p[3]) / 255.0f);
      f32 w[4];
      for (i32 c = 0; c < 4; c++)
        w[c] = (f32)b[c] / 255.0f * (1.0f - painted) + (f32)p[c] / 255.0f;
      for (i32 c = t.desc.layer_count; c < 4; c++)
        w[c] = 0.0f;
      normalize_weights(w, &t.splat[k]);
    }
}

void mark_dirty(terrain3d_slot &t, i32 x0, i32 z0, i32 x1, i32 z1) {
  x0 = std::max(x0, 0);
  z0 = std::max(z0, 0);
  x1 = std::min(x1, t.res);
  z1 = std::min(z1, t.res);
  if (x1 <= x0 || z1 <= z0)
    return;
  if (!t.dirty) {
    t.dx0 = x0, t.dz0 = z0, t.dx1 = x1, t.dz1 = z1;
  } else {
    t.dx0 = std::min(t.dx0, x0), t.dz0 = std::min(t.dz0, z0);
    t.dx1 = std::max(t.dx1, x1), t.dz1 = std::max(t.dz1, z1);
  }
  t.dirty = true;
}

i32 pow2_at_most(i32 v) {
  i32 p = 1;
  while (p * 2 <= v)
    p *= 2;
  return p;
}

void reground_scatter(context &ctx, const terrain3d_slot &t, u32 terrain, f32 x0, f32 z0, f32 x1, f32 z1);
void drop_grass(context &ctx, u32 terrain, f32 x0, f32 z0, f32 x1, f32 z1);
} // namespace

// ---------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------

const terrain3d_slot *terrain_of(const context &ctx, terrain3d_handle handle) {
  return slot_in(ctx.world3d.terrains, handle.id);
}

terrain3d_slot *terrain_of(context &ctx, terrain3d_handle handle) { return slot_in(ctx.world3d.terrains, handle.id); }

vec3 terrain_sample_normal(const terrain3d_slot &t, i32 i, i32 j) {
  const f32 dx = (sample_h(t, i + 1, j) - sample_h(t, i - 1, j)) /
                 ((f32)(std::min(i + 1, t.res - 1) - std::max(i - 1, 0)) * t.spacing);
  const f32 dz = (sample_h(t, i, j + 1) - sample_h(t, i, j - 1)) /
                 ((f32)(std::min(j + 1, t.res - 1) - std::max(j - 1, 0)) * t.spacing);
  return normalize(vec3{-dx, 1.0f, -dz});
}

f32 terrain_height_at(const terrain3d_slot &t, f32 x, f32 z) {
  // The same split of each square as the drawn mesh and Jolt's height field:
  // along the diagonal from (i, j) to (i + 1, j + 1).
  const f32 fx = clamp((x - t.desc.origin.x) / t.spacing, 0.0f, (f32)(t.res - 1));
  const f32 fz = clamp((z - t.desc.origin.z) / t.spacing, 0.0f, (f32)(t.res - 1));
  const i32 i = std::min((i32)fx, t.res - 2);
  const i32 j = std::min((i32)fz, t.res - 2);
  const f32 u = fx - (f32)i, v = fz - (f32)j;
  const f32 h00 = sample_h(t, i, j), h11 = sample_h(t, i + 1, j + 1);
  if (v >= u) {
    const f32 h01 = sample_h(t, i, j + 1);
    return h00 + v * (h01 - h00) + u * (h11 - h01);
  }
  const f32 h10 = sample_h(t, i + 1, j);
  return h00 + u * (h10 - h00) + v * (h11 - h10);
}

vec3 terrain_normal_at(const terrain3d_slot &t, f32 x, f32 z) {
  const f32 fx = clamp((x - t.desc.origin.x) / t.spacing, 0.0f, (f32)(t.res - 1));
  const f32 fz = clamp((z - t.desc.origin.z) / t.spacing, 0.0f, (f32)(t.res - 1));
  const i32 i = std::min((i32)fx, t.res - 2);
  const i32 j = std::min((i32)fz, t.res - 2);
  const f32 u = fx - (f32)i, v = fz - (f32)j;
  const vec3 n = lerp(lerp(terrain_sample_normal(t, i, j), terrain_sample_normal(t, i + 1, j), u),
                      lerp(terrain_sample_normal(t, i, j + 1), terrain_sample_normal(t, i + 1, j + 1), u), v);
  return normalize(n);
}

terrain3d_handle terrain3d_create(context &ctx, const terrain3d_desc &desc) {
  if (refuse(finite3(desc.origin) && std::isfinite(desc.size) && std::isfinite(desc.height_scale), "terrain3d_create"))
    return {};
  if (!(desc.size > 0.0f) || desc.resolution < 3) {
    NJIN_WARN("world3d: terrain3d_create needs size > 0 and resolution >= 3");
    return {};
  }
  terrain3d_slot t;
  t.alive = true;
  t.desc = desc;
  t.desc.heights = nullptr;
  t.desc.heightmap = nullptr;
  t.desc.splatmap = nullptr;
  t.desc.layer_count = std::clamp(desc.layer_count, 1, terrain3d_layer_max);
  t.res = std::min(desc.resolution, 4097);
  t.spacing = desc.size / (f32)(t.res - 1);
  t.chunk = std::clamp(pow2_at_most(std::max(desc.chunk_quads, 8)), 8, 128);
  t.chunks = (t.res - 1 + t.chunk - 1) / t.chunk;
  i32 max_lods = 1;
  while ((1 << max_lods) <= t.chunk && max_lods < 6)
    max_lods++;
  t.lods = std::clamp(desc.lod_levels, 1, max_lods);
  const usize count = (usize)t.res * (usize)t.res;
  t.heights.assign(count, desc.origin.y);

  bool loaded = false;
  if (desc.heights != nullptr) {
    bool bad = false;
    for (usize k = 0; k < count; k++) {
      const f32 h = desc.heights[k];
      bad |= !std::isfinite(h);
      t.heights[k] = desc.origin.y + (std::isfinite(h) ? h : 0.0f);
    }
    refuse(!bad, "terrain3d_desc::heights");
    loaded = true;
  } else if (desc.heightmap != nullptr) {
    std::vector<f32> unit;
    if (load_heightmap(desc.heightmap, t.res, unit)) {
      for (usize k = 0; k < count; k++)
        t.heights[k] = desc.origin.y + unit[k] * desc.height_scale;
      loaded = true;
    }
  }
  if (!loaded) {
    std::vector<f32> unit(count);
    for (i32 j = 0; j < t.res; j++)
      for (i32 i = 0; i < t.res; i++)
        unit[(usize)j * (usize)t.res + (usize)i] = noise_2d(desc.noise, desc.origin.x + (f32)i * t.spacing,
                                                             desc.origin.z + (f32)j * t.spacing);
    const auto [lo, hi] = std::minmax_element(unit.begin(), unit.end());
    const f32 a = *lo, span = std::max(*hi - *lo, 1e-6f);
    for (usize k = 0; k < count; k++)
      t.heights[k] = desc.origin.y + (unit[k] - a) / span * desc.height_scale;
  }
  for (i32 pass = 0; pass < std::max(desc.smooth, 0); pass++) {
    const std::vector<f32> old = t.heights;
    for (i32 j = 0; j < t.res; j++)
      for (i32 i = 0; i < t.res; i++) {
        f32 sum = 0.0f;
        i32 n = 0;
        for (i32 dz = -1; dz <= 1; dz++)
          for (i32 dx = -1; dx <= 1; dx++) {
            const i32 x = i + dx, z = j + dz;
            if (x < 0 || z < 0 || x >= t.res || z >= t.res)
              continue;
            sum += old[(usize)z * (usize)t.res + (usize)x];
            n++;
          }
        t.heights[(usize)j * (usize)t.res + (usize)i] = sum / (f32)n;
      }
  }
  const auto [lo, hi] = std::minmax_element(t.heights.begin(), t.heights.end());
  t.min_h = *lo;
  t.max_h = *hi;

  t.base.assign(count * 4, 0);
  t.paint.assign(count * 4, 0);
  t.splat.assign(count * 4, 0);
  bool splat = false;
  if (!desc.auto_splat && desc.splatmap != nullptr)
    splat = load_splatmap(desc.splatmap, t.res, t.base);
  if (desc.auto_splat)
    rule_weights(t, 0, 0, t.res, t.res);
  else if (!splat)
    for (usize k = 0; k < count; k++)
      t.base[k * 4] = 255;
  mix_splat(t, 0, 0, t.res, t.res);
  mark_dirty(t, 0, 0, t.res, t.res);

  // Tiled images want mipmaps, or the far ground shimmers.
  for (i32 l = 0; l < t.desc.layer_count; l++) {
    if (t.desc.layers[l].albedo.id != 0)
      texture_set_filter(ctx, t.desc.layers[l].albedo, filter_mipmap);
    if (t.desc.layers[l].normal.id != 0)
      texture_set_filter(ctx, t.desc.layers[l].normal, filter_mipmap);
  }
  if (desc.collision)
    t.body = physics3d_heightfield_create(ctx, t.heights.data(), t.res, desc.origin, t.spacing, t.min_h, t.max_h,
                                          desc.friction, desc.user);
  ctx.world3d.terrains.push_back(std::move(t));
  return terrain3d_handle{(u32)ctx.world3d.terrains.size()};
}

void terrain3d_destroy(context &ctx, terrain3d_handle handle) {
  terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr)
    return;
  if (t->body.id != 0)
    body3d_destroy(ctx, t->body);
  world3d_free_terrain(*t);
  *t = terrain3d_slot{};
  // What grew on it goes too.
  for (grass3d_slot &g : ctx.world3d.grasses)
    if (g.alive && g.desc.terrain.id == handle.id)
      world3d_free_grass(g);
}

void draw_terrain3d(const context &ctx, terrain3d_handle handle) {
  if (terrain_of(ctx, handle) != nullptr)
    render3d_record_world(ctx, world3d_terrain, handle.id);
}

f32 terrain3d_height(const context &ctx, terrain3d_handle handle, f32 x, f32 z) {
  const terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr || refuse(std::isfinite(x) && std::isfinite(z), "terrain3d_height"))
    return 0.0f;
  return terrain_height_at(*t, x, z);
}

vec3 terrain3d_normal(const context &ctx, terrain3d_handle handle, f32 x, f32 z) {
  const terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr || refuse(std::isfinite(x) && std::isfinite(z), "terrain3d_normal"))
    return {0.0f, 1.0f, 0.0f};
  return terrain_normal_at(*t, x, z);
}

bool terrain3d_contains(const context &ctx, terrain3d_handle handle, f32 x, f32 z) {
  const terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr)
    return false;
  const vec3 o = t->desc.origin;
  return x >= o.x && z >= o.z && x <= o.x + t->desc.size && z <= o.z + t->desc.size;
}

f32 terrain3d_layer_weight(const context &ctx, terrain3d_handle handle, f32 x, f32 z, i32 layer) {
  const terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr || layer < 0 || layer >= terrain3d_layer_max || !std::isfinite(x) || !std::isfinite(z))
    return 0.0f;
  const f32 fx = clamp((x - t->desc.origin.x) / t->spacing, 0.0f, (f32)(t->res - 1));
  const f32 fz = clamp((z - t->desc.origin.z) / t->spacing, 0.0f, (f32)(t->res - 1));
  const i32 i = std::min((i32)fx, t->res - 2);
  const i32 j = std::min((i32)fz, t->res - 2);
  const f32 u = fx - (f32)i, v = fz - (f32)j;
  const auto w = [&](i32 a, i32 b) {
    return (f32)t->splat[((usize)b * (usize)t->res + (usize)a) * 4 + (usize)layer] / 255.0f;
  };
  return lerp(lerp(w(i, j), w(i + 1, j), u), lerp(w(i, j + 1), w(i + 1, j + 1), u), v);
}

body3d_handle terrain3d_body(const context &ctx, terrain3d_handle handle) {
  const terrain3d_slot *t = terrain_of(ctx, handle);
  return t != nullptr ? t->body : body3d_handle{};
}

namespace {
// The samples a circle of `radius` round (x, z) touches, as [i0, i1) x [j0, j1).
bool circle_samples(const terrain3d_slot &t, f32 x, f32 z, f32 radius, i32 &i0, i32 &j0, i32 &i1, i32 &j1) {
  i0 = std::max(0, (i32)std::floor((x - radius - t.desc.origin.x) / t.spacing));
  j0 = std::max(0, (i32)std::floor((z - radius - t.desc.origin.z) / t.spacing));
  i1 = std::min(t.res, (i32)std::ceil((x + radius - t.desc.origin.x) / t.spacing) + 1);
  j1 = std::min(t.res, (i32)std::ceil((z + radius - t.desc.origin.z) / t.spacing) + 1);
  return i1 > i0 && j1 > j0;
}

f32 brush_weight(f32 d, f32 radius, f32 falloff) {
  const f32 inner = radius * (1.0f - clamp(falloff, 0.0f, 1.0f));
  return 1.0f - smooth(inner, radius, d);
}
} // namespace

void terrain3d_edit(context &ctx, terrain3d_handle handle, const terrain3d_brush &brush) {
  terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr || refuse(finite3(brush.center) && std::isfinite(brush.radius) && std::isfinite(brush.strength) &&
                                 std::isfinite(brush.height),
                             "terrain3d_edit"))
    return;
  const f32 radius = std::max(brush.radius, t->spacing * 0.5f);
  i32 i0, j0, i1, j1;
  if (!circle_samples(*t, brush.center.x, brush.center.z, radius, i0, j0, i1, j1))
    return;
  const std::vector<f32> old = t->heights;
  const f32 k = clamp(brush.strength, 0.0f, 1.0f);
  for (i32 j = j0; j < j1; j++)
    for (i32 i = i0; i < i1; i++) {
      const f32 x = t->desc.origin.x + (f32)i * t->spacing, z = t->desc.origin.z + (f32)j * t->spacing;
      const f32 f = brush_weight(std::hypot(x - brush.center.x, z - brush.center.z), radius, brush.falloff);
      if (f <= 0.0f)
        continue;
      f32 &h = t->heights[(usize)j * (usize)t->res + (usize)i];
      switch (brush.kind) {
      case terrain3d_raise:
        h += brush.strength * f;
        break;
      case terrain3d_lower:
        h -= brush.strength * f;
        break;
      case terrain3d_flatten:
        h = lerp(h, brush.height, k * f);
        break;
      case terrain3d_smooth: {
        f32 sum = 0.0f;
        i32 n = 0;
        for (i32 dz = -1; dz <= 1; dz++)
          for (i32 dx = -1; dx <= 1; dx++) {
            const i32 a = i + dx, b = j + dz;
            if (a < 0 || b < 0 || a >= t->res || b >= t->res)
              continue;
            sum += old[(usize)b * (usize)t->res + (usize)a];
            n++;
          }
        h = lerp(h, sum / (f32)n, k * f);
        break;
      }
      }
      t->min_h = std::min(t->min_h, h);
      t->max_h = std::max(t->max_h, h);
    }
  // The normals one sample round the edit change too, and with them the rules.
  const i32 a0 = std::max(i0 - 1, 0), b0 = std::max(j0 - 1, 0);
  const i32 a1 = std::min(i1 + 1, t->res), b1 = std::min(j1 + 1, t->res);
  if (t->desc.auto_splat)
    rule_weights(*t, a0, b0, a1, b1);
  mix_splat(*t, a0, b0, a1, b1);
  mark_dirty(*t, a0, b0, a1, b1);
  if (t->body.id != 0)
    physics3d_heightfield_set(ctx, t->body, t->heights.data(), t->res, i0, j0, i1, j1);
  const vec3 o = t->desc.origin;
  const f32 x0 = o.x + (f32)a0 * t->spacing, z0 = o.z + (f32)b0 * t->spacing;
  const f32 x1 = o.x + (f32)a1 * t->spacing, z1 = o.z + (f32)b1 * t->spacing;
  reground_scatter(ctx, *t, handle.id, x0, z0, x1, z1);
  drop_grass(ctx, handle.id, x0, z0, x1, z1);
}

void terrain3d_paint(context &ctx, terrain3d_handle handle, vec3 center, f32 radius, i32 layer, f32 strength) {
  terrain3d_slot *t = terrain_of(ctx, handle);
  if (t == nullptr || layer < 0 || layer >= t->desc.layer_count ||
      refuse(finite3(center) && std::isfinite(radius) && std::isfinite(strength), "terrain3d_paint"))
    return;
  radius = std::max(radius, t->spacing * 0.5f);
  i32 i0, j0, i1, j1;
  if (!circle_samples(*t, center.x, center.z, radius, i0, j0, i1, j1))
    return;
  const f32 s = clamp(strength, 0.0f, 1.0f);
  for (i32 j = j0; j < j1; j++)
    for (i32 i = i0; i < i1; i++) {
      const f32 x = t->desc.origin.x + (f32)i * t->spacing, z = t->desc.origin.z + (f32)j * t->spacing;
      const f32 f = brush_weight(std::hypot(x - center.x, z - center.z), radius, 0.5f) * s;
      if (f <= 0.0f)
        continue;
      u8 *p = &t->paint[((usize)j * (usize)t->res + (usize)i) * 4];
      // Painting covers what was there: the other painted layers fade, and the
      // base weights give way in mix_splat as the paint adds up.
      f32 sum = 0.0f;
      for (i32 c = 0; c < 4; c++)
        sum += (f32)p[c];
      for (i32 c = 0; c < 4; c++)
        if (c != layer)
          p[c] = (u8)std::lround((f32)p[c] * (1.0f - f));
      const f32 room = 255.0f - std::min(255.0f, sum - (f32)p[layer]);
      p[layer] = (u8)std::lround(std::min((f32)p[layer] + f * 255.0f, std::max(room, (f32)p[layer]) + f * 255.0f));
      if ((i32)p[0] + p[1] + p[2] + p[3] > 255) {
        const f32 over = 255.0f / (f32)((i32)p[0] + p[1] + p[2] + p[3]);
        for (i32 c = 0; c < 4; c++)
          p[c] = (u8)std::floor((f32)p[c] * over);
      }
    }
  mix_splat(*t, i0, j0, i1, j1);
  mark_dirty(*t, i0, j0, i1, j1);
  const vec3 o = t->desc.origin;
  drop_grass(ctx, handle.id, o.x + (f32)i0 * t->spacing, o.z + (f32)j0 * t->spacing, o.x + (f32)i1 * t->spacing,
             o.z + (f32)j1 * t->spacing);
}

// ---------------------------------------------------------------------------
// Grass
// ---------------------------------------------------------------------------

void grass_cell_blades(const terrain3d_slot &t, const grass3d_slot &g, i32 cx, i32 cz, std::vector<f32> &out) {
  out.clear();
  const grass3d_desc &d = g.desc;
  const f32 x0 = t.desc.origin.x + (f32)cx * g.cell, z0 = t.desc.origin.z + (f32)cz * g.cell;
  const i32 tries = (i32)std::lround(std::max(d.density, 0.0f) * g.cell * g.cell);
  hash_rng r(((u64)d.seed << 40) ^ ((u64)(u32)cx << 20) ^ (u64)(u32)cz ^ 0x5bd1e995ULL);
  const f32 cos_max = std::cos(clamp(d.max_slope, 0.0f, 90.0f) * (pi / 180.0f));
  const f32 cos_soft = std::cos(clamp(d.max_slope - 6.0f, 0.0f, 90.0f) * (pi / 180.0f));
  const f32 size = t.desc.size;
  out.reserve((usize)tries * 8);
  for (i32 k = 0; k < tries; k++) {
    const f32 x = x0 + r.unit() * g.cell, z = z0 + r.unit() * g.cell;
    const f32 keep = r.unit();
    const f32 facing = r.unit() * 2.0f * pi;
    const f32 tall = r.unit(), shade = r.unit(), phase = r.unit();
    if (x < t.desc.origin.x || z < t.desc.origin.z || x > t.desc.origin.x + size || z > t.desc.origin.z + size)
      continue;
    f32 chance = 1.0f;
    if (d.layer >= 0 && d.layer < terrain3d_layer_max) {
      const f32 fx = clamp((x - t.desc.origin.x) / t.spacing, 0.0f, (f32)(t.res - 1));
      const f32 fz = clamp((z - t.desc.origin.z) / t.spacing, 0.0f, (f32)(t.res - 1));
      const i32 i = (i32)std::lround(fx), j = (i32)std::lround(fz);
      chance *= (f32)t.splat[((usize)j * (usize)t.res + (usize)i) * 4 + (usize)d.layer] / 255.0f;
    }
    if (chance <= keep)
      continue;
    const vec3 n = terrain_normal_at(t, x, z);
    chance *= smooth(cos_max, cos_soft, n.y);
    if (d.patchiness > 0.0f)
      chance *= lerp(1.0f, smooth(0.38f, 0.62f, noise_2d(d.patches, x, z)), clamp(d.patchiness, 0.0f, 1.0f));
    if (chance <= keep)
      continue;
    const f32 h = std::max(d.height * (1.0f + d.height_jitter * (tall * 2.0f - 1.0f)), 0.01f);
    out.insert(out.end(), {x, terrain_height_at(t, x, z), z, h, facing, std::max(d.width, 0.001f),
                           1.0f + d.color_jitter * (shade * 2.0f - 1.0f), phase * 6.2831853f});
  }
}

namespace {
// Cells under a change grow again on their next draw.
void drop_grass(context &ctx, u32 terrain, f32 x0, f32 z0, f32 x1, f32 z1) {
  const terrain3d_slot *t = slot_in(ctx.world3d.terrains, terrain);
  if (t == nullptr)
    return;
  for (grass3d_slot &g : ctx.world3d.grasses) {
    if (!g.alive || g.desc.terrain.id != terrain)
      continue;
    for (grass_cell &c : g.cells) {
      const f32 cx0 = t->desc.origin.x + (f32)c.cx * g.cell, cz0 = t->desc.origin.z + (f32)c.cz * g.cell;
      if (cx0 + g.cell >= x0 && cz0 + g.cell >= z0 && cx0 <= x1 && cz0 <= z1)
        c.stale = true;
    }
  }
}
} // namespace

grass3d_handle grass3d_create(context &ctx, const grass3d_desc &desc) {
  const terrain3d_slot *t = terrain_of(ctx, desc.terrain);
  if (t == nullptr) {
    NJIN_WARN("world3d: grass3d_create on an invalid terrain");
    return {};
  }
  grass3d_slot g;
  g.alive = true;
  g.desc = desc;
  // Few enough blades per cell that one is quick to make as the camera moves.
  g.cell = desc.density * 16.0f * 16.0f > 6000.0f ? 8.0f : 16.0f;
  ctx.world3d.grasses.push_back(std::move(g));
  return grass3d_handle{(u32)ctx.world3d.grasses.size()};
}

void grass3d_destroy(context &ctx, grass3d_handle handle) {
  grass3d_slot *g = slot_in(ctx.world3d.grasses, handle.id);
  if (g == nullptr)
    return;
  world3d_free_grass(*g);
  *g = grass3d_slot{};
}

void draw_grass3d(const context &ctx, grass3d_handle handle) {
  const grass3d_slot *g = slot_in(ctx.world3d.grasses, handle.id);
  if (g != nullptr && terrain_of(ctx, g->desc.terrain) != nullptr)
    render3d_record_world(ctx, world3d_grass, handle.id);
}

i32 grass3d_drawn(const context &ctx, grass3d_handle handle) {
  const grass3d_slot *g = slot_in(ctx.world3d.grasses, handle.id);
  return g != nullptr ? g->drawn : 0;
}

void wind3d_set(context &ctx, vec2 wind) {
  if (!refuse(std::isfinite(wind.x) && std::isfinite(wind.y), "wind3d_set"))
    ctx.world3d.wind = wind;
}

vec2 wind3d_get(const context &ctx) { return ctx.world3d.wind; }

// ---------------------------------------------------------------------------
// Scatter
// ---------------------------------------------------------------------------

namespace {
// Instance data for draw_instanced3d (njin_3d.h): position and scale, colour,
// rotation in degrees (z, then x, then y), per-axis scale.
void scatter_instance(const terrain3d_slot &t, const scatter3d_desc &d, const scatter_item &it, f32 *out) {
  const f32 y = terrain_height_at(t, it.x, it.z) - d.sink * it.scale;
  // The up axis leaned toward the ground's normal by `align`, then turned
  // about it by the yaw: tilt found in the frame before the yaw.
  const vec3 n = terrain_normal_at(t, it.x, it.z);
  const vec3 up = normalize(lerp(vec3{0.0f, 1.0f, 0.0f}, n, clamp(d.align, 0.0f, 1.0f)));
  const f32 c = std::cos(-it.yaw), s = std::sin(-it.yaw);
  const vec3 local{up.x * c + up.z * s, up.y, -up.x * s + up.z * c};
  const f32 roll = std::asin(clamp(-local.x, -1.0f, 1.0f));
  const f32 pitch = std::atan2(local.z, local.y);
  const f32 deg = 180.0f / pi;
  const rgba col = d.color;
  const f32 k = it.shade;
  const f32 data[16] = {it.x,        y,           it.z,        it.scale,   col.r * k, col.g * k, col.b * k, col.a,
                        pitch * deg, it.yaw * deg, roll * deg, 0.0f,      1.0f,      1.0f,      1.0f,      0.0f};
  std::copy(std::begin(data), std::end(data), out);
}

void upload_cell(context &ctx, const terrain3d_slot &t, const scatter3d_desc &d, scatter_cell &c) {
  if (c.items.empty())
    return;
  std::vector<f32> data(c.items.size() * 16);
  c.lo = {1e30f, 1e30f, 1e30f};
  c.hi = {-1e30f, -1e30f, -1e30f};
  for (usize i = 0; i < c.items.size(); i++) {
    scatter_instance(t, d, c.items[i], &data[i * 16]);
    const vec3 p{data[i * 16], data[i * 16 + 1], data[i * 16 + 2]};
    c.lo = {std::min(c.lo.x, p.x), std::min(c.lo.y, p.y), std::min(c.lo.z, p.z)};
    c.hi = {std::max(c.hi.x, p.x), std::max(c.hi.y, p.y), std::max(c.hi.z, p.z)};
  }
  if (c.buffer.id == 0)
    c.buffer = instance_buffer_create(ctx, 16);
  instance_buffer_upload(ctx, c.buffer, data.data(), (u32)c.items.size());
}

void reground_scatter(context &ctx, const terrain3d_slot &t, u32 terrain, f32 x0, f32 z0, f32 x1, f32 z1) {
  for (scatter3d_slot &s : ctx.world3d.scatters) {
    if (!s.alive || s.desc.terrain.id != terrain)
      continue;
    for (scatter_cell &c : s.cells) {
      if (c.items.empty())
        continue;
      const f32 cx0 = c.lo.x - 1.0f, cz0 = c.lo.z - 1.0f, cx1 = c.hi.x + 1.0f, cz1 = c.hi.z + 1.0f;
      if (cx1 < x0 || cz1 < z0 || cx0 > x1 || cz0 > z1)
        continue;
      upload_cell(ctx, t, s.desc, c);
    }
  }
}
} // namespace

scatter3d_handle scatter3d_create(context &ctx, const scatter3d_desc &desc) {
  const terrain3d_slot *t = terrain_of(ctx, desc.terrain);
  if (t == nullptr || desc.model.id == 0) {
    NJIN_WARN("world3d: scatter3d_create needs a valid terrain and model");
    return {};
  }
  scatter3d_slot s;
  s.alive = true;
  s.desc = desc;
  s.cell = 64.0f;
  s.per_side = std::max(1, (i32)std::ceil(t->desc.size / s.cell));
  s.cells.resize((usize)s.per_side * (usize)s.per_side);
  // A jittered grid `spacing` apart, each candidate kept by chance and by the
  // rules, then dropped when it comes nearer than `spacing` to one kept.
  const f32 step = std::max(desc.spacing, 0.05f);
  const i32 n = std::max(1, (i32)std::ceil(t->desc.size / step));
  const f32 keep = clamp(desc.density * step * step, 0.0f, 1.0f);
  const f32 cos_lo = std::cos(clamp(desc.max_slope, 0.0f, 90.0f) * (pi / 180.0f));
  const f32 cos_hi = std::cos(clamp(desc.min_slope, 0.0f, 90.0f) * (pi / 180.0f));
  std::vector<i32> grid((usize)n * (usize)n, -1); // index into `placed` per grid square
  std::vector<scatter_item> placed;
  hash_rng r(((u64)desc.seed << 32) ^ 0x2545f4914f6cdd1dULL);
  for (i32 gz = 0; gz < n; gz++)
    for (i32 gx = 0; gx < n; gx++) {
      scatter_item it;
      it.x = t->desc.origin.x + ((f32)gx + r.unit()) * step;
      it.z = t->desc.origin.z + ((f32)gz + r.unit()) * step;
      const f32 roll = r.unit();
      it.yaw = r.unit() * 2.0f * pi;
      it.scale = lerp(desc.scale_min, desc.scale_max, r.unit());
      it.shade = 1.0f + desc.color_jitter * (r.unit() * 2.0f - 1.0f);
      if (it.x > t->desc.origin.x + t->desc.size || it.z > t->desc.origin.z + t->desc.size)
        continue;
      f32 chance = keep;
      const f32 h = terrain_height_at(*t, it.x, it.z);
      const vec3 nrm = terrain_normal_at(*t, it.x, it.z);
      if (h < desc.min_height || h > desc.max_height || nrm.y < cos_lo || nrm.y > cos_hi)
        continue;
      if (desc.layer >= 0 && terrain3d_layer_weight(ctx, desc.terrain, it.x, it.z, desc.layer) < desc.layer_min)
        continue;
      if (desc.patchiness > 0.0f)
        chance *= lerp(1.0f, smooth(0.4f, 0.6f, noise_2d(desc.patches, it.x, it.z)),
                       clamp(desc.patchiness, 0.0f, 1.0f));
      if (roll >= chance)
        continue;
      bool clear = true;
      for (i32 dz = -1; dz <= 1 && clear; dz++)
        for (i32 dx = -1; dx <= 1 && clear; dx++) {
          const i32 a = gx + dx, b = gz + dz;
          if (a < 0 || b < 0 || a >= n || b >= n)
            continue;
          const i32 other = grid[(usize)b * (usize)n + (usize)a];
          if (other >= 0 && std::hypot(placed[(usize)other].x - it.x, placed[(usize)other].z - it.z) < step)
            clear = false;
        }
      if (!clear)
        continue;
      grid[(usize)gz * (usize)n + (usize)gx] = (i32)placed.size();
      placed.push_back(it);
    }
  for (const scatter_item &it : placed) {
    const i32 cx = std::clamp((i32)((it.x - t->desc.origin.x) / s.cell), 0, s.per_side - 1);
    const i32 cz = std::clamp((i32)((it.z - t->desc.origin.z) / s.cell), 0, s.per_side - 1);
    s.cells[(usize)cz * (usize)s.per_side + (usize)cx].items.push_back(it);
  }
  s.total = (i32)placed.size();
  for (scatter_cell &c : s.cells)
    upload_cell(ctx, *t, s.desc, c);
  ctx.world3d.scatters.push_back(std::move(s));
  return scatter3d_handle{(u32)ctx.world3d.scatters.size()};
}

void scatter3d_destroy(context &ctx, scatter3d_handle handle) {
  scatter3d_slot *s = slot_in(ctx.world3d.scatters, handle.id);
  if (s == nullptr)
    return;
  for (scatter_cell &c : s->cells)
    if (c.buffer.id != 0)
      instance_buffer_destroy(ctx, c.buffer);
  *s = scatter3d_slot{};
}

void draw_scatter3d(const context &ctx, scatter3d_handle handle) {
  const scatter3d_slot *s = slot_in(ctx.world3d.scatters, handle.id);
  const render3d_state &r = ctx.render3d;
  if (s == nullptr || !r.active || terrain_of(ctx, s->desc.terrain) == nullptr)
    return;
  const vec3 eye = r.camera.position;
  for (const scatter_cell &c : s->cells) {
    if (c.items.empty() || c.buffer.id == 0)
      continue;
    // Room for the model's own size round the instance positions.
    const vec3 pad{8.0f, 30.0f, 8.0f};
    const vec3 lo = c.lo - pad, hi = c.hi + pad;
    const vec3 near{clamp(eye.x, lo.x, hi.x), clamp(eye.y, lo.y, hi.y), clamp(eye.z, lo.z, hi.z)};
    const f32 d = distance(eye, near);
    if (d > s->desc.draw_distance || !render3d_box_visible(r, lo, hi))
      continue;
    const model_handle m = d > s->desc.lod_distance && s->desc.far_model.id != 0 ? s->desc.far_model : s->desc.model;
    draw_instanced3d(ctx, m, c.buffer, 0, (u32)c.items.size());
  }
}

i32 scatter3d_count(const context &ctx, scatter3d_handle handle) {
  const scatter3d_slot *s = slot_in(ctx.world3d.scatters, handle.id);
  return s != nullptr ? s->total : 0;
}

i32 scatter3d_transforms(const context &ctx, scatter3d_handle handle, transform3d *out, i32 count) {
  const scatter3d_slot *s = slot_in(ctx.world3d.scatters, handle.id);
  const terrain3d_slot *t = s != nullptr ? terrain_of(ctx, s->desc.terrain) : nullptr;
  if (s == nullptr || t == nullptr)
    return 0;
  i32 k = 0;
  for (const scatter_cell &c : s->cells)
    for (const scatter_item &it : c.items) {
      if (out != nullptr && k < count) {
        f32 data[16];
        scatter_instance(*t, s->desc, it, data);
        out[k] = transform3d{.position = {data[0], data[1], data[2]},
                             .rotation = {data[8], data[9], data[10]},
                             .scale = {data[3], data[3], data[3]}};
      }
      k++;
    }
  return k;
}

// ---------------------------------------------------------------------------
// Water
// ---------------------------------------------------------------------------

void water_displace(const water3d_desc &w, f32 x, f32 z, f32 t, vec3 &offset, vec3 &normal) {
  offset = {};
  vec3 tangent{1.0f, 0.0f, 0.0f}, binormal{0.0f, 0.0f, 1.0f};
  const i32 count = std::clamp(w.wave_count, 0, water3d_wave_max);
  for (i32 i = 0; i < count; i++) {
    const water3d_wave &wave = w.waves[i];
    const f32 len = length(wave.direction);
    if (len <= 1e-6f)
      continue;
    const vec2 d = wave.direction / len;
    const f32 k = 2.0f * pi / std::max(wave.wavelength, 0.01f);
    const f32 c = std::sqrt(gravity / k) * wave.speed;
    const f32 s = clamp(wave.steepness, 0.0f, 1.0f);
    const f32 a = s / k;
    const f32 f = k * (d.x * x + d.y * z - c * t);
    const f32 cs = std::cos(f), sn = std::sin(f);
    offset += vec3{d.x * a * cs, a * sn, d.y * a * cs};
    tangent += vec3{-d.x * d.x * s * sn, d.x * s * cs, -d.x * d.y * s * sn};
    binormal += vec3{-d.x * d.y * s * sn, d.y * s * cs, -d.y * d.y * s * sn};
  }
  normal = normalize(cross(binormal, tangent));
}

namespace {
const water3d_slot *water_of(const context &ctx, u32 id) { return slot_in(ctx.world3d.waters, id); }

bool on_lake(const water3d_desc &w, f32 x, f32 z) {
  if (w.size.x <= 0.0f || w.size.y <= 0.0f)
    return true;
  return std::fabs(x - w.center.x) <= w.size.x * 0.5f && std::fabs(z - w.center.y) <= w.size.y * 0.5f;
}

// The rest position whose wave lands on (x, z): the waves move the water
// sideways too, so the surface above a point comes from elsewhere. Fixed
// point iteration converges while the steepnesses sum below 1.
void surface_at(const water3d_desc &w, f32 x, f32 z, f32 t, vec3 &offset, vec3 &normal) {
  f32 px = x, pz = z;
  for (i32 i = 0; i < 8; i++) {
    water_displace(w, px, pz, t, offset, normal);
    px = x - offset.x;
    pz = z - offset.z;
  }
  water_displace(w, px, pz, t, offset, normal);
}
} // namespace

water3d_handle water3d_create(context &ctx, const water3d_desc &desc) {
  water3d_slot w;
  w.alive = true;
  w.desc = desc;
  if (refuse(std::isfinite(desc.level) && std::isfinite(desc.center.x) && std::isfinite(desc.center.y),
             "water3d_create"))
    w.desc.level = 0.0f, w.desc.center = {};
  ctx.world3d.waters.push_back(w);
  return water3d_handle{(u32)ctx.world3d.waters.size()};
}

void water3d_destroy(context &ctx, water3d_handle handle) {
  water3d_slot *w = slot_in(ctx.world3d.waters, handle.id);
  if (w == nullptr)
    return;
  physics3d_unfloat(ctx, body3d_handle{}, handle.id);
  *w = water3d_slot{};
}

void water3d_set(context &ctx, water3d_handle handle, const water3d_desc &desc) {
  water3d_slot *w = slot_in(ctx.world3d.waters, handle.id);
  if (w != nullptr && !refuse(std::isfinite(desc.level), "water3d_set"))
    w->desc = desc;
}

water3d_desc water3d_get(const context &ctx, water3d_handle handle) {
  const water3d_slot *w = water_of(ctx, handle.id);
  return w != nullptr ? w->desc : water3d_desc{};
}

void draw_water3d(const context &ctx, water3d_handle handle) {
  if (water_of(ctx, handle.id) != nullptr)
    render3d_record_world(ctx, world3d_water, handle.id);
}

f32 water3d_height(const context &ctx, water3d_handle handle, f32 x, f32 z) {
  const water3d_slot *w = water_of(ctx, handle.id);
  if (w == nullptr || refuse(std::isfinite(x) && std::isfinite(z), "water3d_height"))
    return 0.0f;
  if (!on_lake(w->desc, x, z))
    return w->desc.level;
  vec3 offset{}, normal{};
  surface_at(w->desc, x, z, ctx.world3d.time, offset, normal);
  return w->desc.level + offset.y;
}

vec3 water3d_normal(const context &ctx, water3d_handle handle, f32 x, f32 z) {
  const water3d_slot *w = water_of(ctx, handle.id);
  if (w == nullptr || !std::isfinite(x) || !std::isfinite(z) || !on_lake(w->desc, x, z))
    return {0.0f, 1.0f, 0.0f};
  vec3 offset{}, normal{};
  surface_at(w->desc, x, z, ctx.world3d.time, offset, normal);
  return normal;
}

f32 water3d_time(const context &ctx) { return ctx.world3d.time; }

bool world3d_water_surface(const context &ctx, u32 water, vec3 at, vec3 &point, vec3 &normal) {
  const water3d_slot *w = water_of(ctx, water);
  if (w == nullptr || !on_lake(w->desc, at.x, at.z))
    return false;
  vec3 offset{};
  surface_at(w->desc, at.x, at.z, ctx.world3d.time, offset, normal);
  point = {at.x, w->desc.level + offset.y, at.z};
  return true;
}

void water3d_float(context &ctx, water3d_handle water, body3d_handle body, const buoyancy3d &b) {
  if (water_of(ctx, water.id) == nullptr ||
      refuse(std::isfinite(b.buoyancy) && std::isfinite(b.linear_drag) && std::isfinite(b.angular_drag) &&
                 finite3(b.flow),
             "water3d_float"))
    return;
  physics3d_float(ctx, body, water.id, std::max(b.buoyancy, 0.0f), std::max(b.linear_drag, 0.0f),
                  std::max(b.angular_drag, 0.0f), b.flow, world3d_water_surface);
}

void water3d_unfloat(context &ctx, water3d_handle water, body3d_handle body) {
  if (water.id != 0 && body.id != 0)
    physics3d_unfloat(ctx, body, water.id);
}

// ---------------------------------------------------------------------------
// Sky and weather
// ---------------------------------------------------------------------------

weather3d weather3d_preset(weather3d_kind kind) {
  switch (kind) {
  case weather3d_overcast:
    return {.clouds = 0.92f, .cloud_darkness = 0.35f, .fog = 0.004f, .rain = 0.0f, .snow = 0.0f,
            .wind = {4.0f, 1.0f}, .wetness = 0.0f};
  case weather3d_rain:
    return {.clouds = 1.0f, .cloud_darkness = 0.65f, .fog = 0.009f, .rain = 1.0f, .snow = 0.0f,
            .wind = {5.0f, 1.5f}, .wetness = 1.0f};
  case weather3d_snow:
    return {.clouds = 0.95f, .cloud_darkness = 0.2f, .fog = 0.012f, .rain = 0.0f, .snow = 1.0f,
            .wind = {1.5f, 0.5f}, .wetness = 0.2f};
  case weather3d_fog:
    return {.clouds = 0.6f, .cloud_darkness = 0.15f, .fog = 0.03f, .rain = 0.0f, .snow = 0.0f,
            .wind = {0.6f, 0.2f}, .wetness = 0.3f};
  case weather3d_clear:
  default:
    return {.clouds = 0.25f, .cloud_darkness = 0.0f, .fog = 0.0f, .rain = 0.0f, .snow = 0.0f,
            .wind = {2.0f, 0.6f}, .wetness = 0.0f};
  }
}

weather3d weather3d_lerp(const weather3d &a, const weather3d &b, f32 t) {
  t = clamp(t, 0.0f, 1.0f);
  return {.clouds = lerp(a.clouds, b.clouds, t),
          .cloud_darkness = lerp(a.cloud_darkness, b.cloud_darkness, t),
          .fog = lerp(a.fog, b.fog, t),
          .rain = lerp(a.rain, b.rain, t),
          .snow = lerp(a.snow, b.snow, t),
          .wind = lerp(a.wind, b.wind, t),
          .wetness = lerp(a.wetness, b.wetness, t)};
}

vec3 sky3d_sun_direction(const sky3d &sky) {
  // The sun on the celestial sphere: hour angle H from noon (westward),
  // declination from the season, turned into the local sky by the latitude.
  // Local axes: south +z, west -x, up +y (sunrise in the east, +x).
  const f32 rad = pi / 180.0f;
  const f32 hour = std::isfinite(sky.hour) ? sky.hour : 12.0f;
  const f32 H = (hour - 12.0f) / 24.0f * 2.0f * pi;
  const f32 dec = clamp(std::isfinite(sky.season) ? sky.season : 0.0f, -1.0f, 1.0f) * 23.44f * rad;
  const f32 lat = clamp(std::isfinite(sky.latitude) ? sky.latitude : 0.0f, -89.0f, 89.0f) * rad;
  const vec3 south{0.0f, 0.0f, 1.0f}, west{-1.0f, 0.0f, 0.0f}, up{0.0f, 1.0f, 0.0f};
  const vec3 meridian = south * std::sin(lat) + up * std::cos(lat); // equator above the south horizon
  const vec3 pole = -south * std::cos(lat) + up * std::sin(lat);
  vec3 v = meridian * (std::cos(dec) * std::cos(H)) + west * (std::cos(dec) * std::sin(H)) + pole * std::sin(dec);
  const f32 turn = (std::isfinite(sky.north) ? sky.north : 0.0f) * rad;
  const f32 c = std::cos(turn), s = std::sin(turn);
  v = {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
  return normalize(v);
}

namespace {
f32 luminance(vec3 c) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; }

// The sunlight left after the air between it and the ground: redder and
// dimmer as the sun nears the horizon (Kasten and Young's air mass).
vec3 sun_transmittance(f32 elevation) {
  const f32 e = std::max(elevation, 0.0f);
  const f32 deg = std::asin(clamp(e, 0.0f, 1.0f)) * (180.0f / pi);
  const f32 mass = 1.0f / (e + 0.50572f * std::pow(deg + 6.07995f, -1.6364f));
  const vec3 beta{0.045f, 0.105f, 0.26f};
  return {std::exp(-beta.x * (mass - 1.0f)), std::exp(-beta.y * (mass - 1.0f)), std::exp(-beta.z * (mass - 1.0f))};
}

vec3 mix3(vec3 a, vec3 b, f32 t) { return lerp(a, b, t); }
} // namespace

sky_params sky_params_of(const sky3d &sky) {
  sky_params p;
  p.sun = sky3d_sun_direction(sky);
  const f32 e = p.sun.y;
  const weather3d &w = sky.weather;
  const f32 cover = clamp(w.clouds, 0.0f, 1.0f);
  p.day = smooth(-0.12f, 0.22f, e);
  p.twilight = std::exp(-std::pow((e - 0.02f) / 0.11f, 2.0f));
  p.zenith = mix3({0.006f, 0.01f, 0.028f}, {0.17f, 0.36f, 0.78f}, p.day);
  p.horizon = mix3({0.025f, 0.035f, 0.065f}, {0.6f, 0.75f, 0.92f}, p.day);
  p.horizon = mix3(p.horizon, {0.92f, 0.52f, 0.32f}, p.twilight * 0.6f);
  p.zenith = mix3(p.zenith, {0.22f, 0.22f, 0.42f}, p.twilight * 0.3f);
  p.glow = {1.0f, 0.42f, 0.16f};
  // Clouds grey the sky; dark ones darken it.
  const f32 bright = lerp(0.04f, 0.72f, p.day) * (1.0f - 0.5f * clamp(w.cloud_darkness, 0.0f, 1.0f));
  p.zenith = mix3(p.zenith, vec3{0.9f, 0.92f, 0.96f} * bright, cover * 0.85f);
  p.horizon = mix3(p.horizon, vec3{0.95f, 0.96f, 0.98f} * bright * 1.08f, cover * 0.85f);
  p.twilight *= 1.0f - cover * 0.8f;
  p.sun_color = sun_transmittance(e) * smooth(-0.03f, 0.04f, e);
  p.ground = p.horizon * 0.42f;
  p.clouds = cover;
  p.cloud_darkness = clamp(w.cloud_darkness, 0.0f, 1.0f);
  p.cloud_height = std::max(sky.cloud_height, 50.0f);
  p.cloud_scale = std::max(sky.cloud_scale, 0.05f);
  p.sun_size = std::max(sky.sun_size, 0.0f);
  p.stars = sky.stars ? (1.0f - smooth(-0.22f, -0.02f, e)) * (1.0f - cover) : 0.0f;
  p.wind = w.wind;
  p.fog = std::max(w.fog, 0.0f);
  p.fog_color = p.horizon;
  p.rain = clamp(w.rain, 0.0f, 1.0f);
  p.snow = clamp(w.snow, 0.0f, 1.0f);
  return p;
}

sky_params sky_params_from_light(const context &ctx) {
  const light3d &l = ctx.render3d.light;
  sky_params p;
  const vec3 d = length_sq(l.direction) > 0.0f ? normalize(l.direction) : vec3{0.0f, -1.0f, 0.0f};
  p.sun = -d;
  p.sun_color = {l.color.r, l.color.g, l.color.b};
  p.horizon = {l.fog_color.r, l.fog_color.g, l.fog_color.b};
  p.zenith = vec3{l.ambient.r, l.ambient.g, l.ambient.b} * 1.15f;
  p.ground = p.horizon * 0.4f;
  p.twilight = 0.0f;
  p.day = 1.0f;
  p.clouds = 0.0f;
  p.stars = 0.0f;
  p.wind = ctx.world3d.wind;
  p.fog = l.fog_density;
  p.fog_color = p.horizon;
  return p;
}

namespace {
// The sky colour along `dir` (no clouds, no stars): the same as the shader's
// sky_color() in world3d_draw.cpp.
vec3 sky_rgb(const sky_params &p, vec3 dir) {
  dir = normalize(dir);
  const f32 up = std::max(dir.y, 0.0f);
  vec3 c = mix3(p.horizon, p.zenith, std::pow(up, 0.5f));
  if (dir.y < 0.0f)
    c = mix3(p.horizon, p.ground, smooth(0.0f, 0.25f, -dir.y));
  const f32 cosang = dot(dir, p.sun);
  const f32 side = std::pow(std::max(cosang * 0.5f + 0.5f, 0.0f), 4.0f);
  c += p.glow * (p.twilight * side * std::pow(1.0f - up, 3.0f) * 0.8f);
  const f32 m = std::max(cosang, 0.0f);
  c += p.sun_color * ((std::pow(m, 600.0f) * 1.2f + std::pow(m, 24.0f) * 0.18f) * (1.0f - p.clouds * 0.7f));
  const f32 r = 0.0047f * std::max(p.sun_size, 0.0f);
  const f32 disc = smooth(std::cos(r * 1.4f), std::cos(r), cosang) * (1.0f - p.clouds) * smooth(-0.02f, 0.01f, dir.y);
  c += p.sun_color * (disc * 4.0f);
  const f32 fog = clamp(p.fog * 25.0f, 0.0f, 1.0f);
  c = mix3(c, p.fog_color, fog * (1.0f - 0.5f * up));
  return {std::min(c.x, 1.0f), std::min(c.y, 1.0f), std::min(c.z, 1.0f)};
}
} // namespace

rgba sky3d_color(const sky3d &sky, vec3 direction) {
  if (!finite3(direction) || length_sq(direction) <= 0.0f)
    direction = {0.0f, 1.0f, 0.0f};
  const vec3 c = sky_rgb(sky_params_of(sky), direction);
  return {c.x, c.y, c.z, 1.0f};
}

light3d sky3d_light(const sky3d &sky, const light3d &base) {
  const sky_params p = sky_params_of(sky);
  light3d l = base;
  const f32 e = p.sun.y;
  const f32 cover = p.clouds;
  // Clouds scatter the direct light: an overcast day has weak, soft shadows.
  const f32 direct = 1.0f - 0.8f * std::pow(cover, 1.5f);
  if (e >= -0.03f) {
    l.direction = -p.sun;
    const vec3 c = p.sun_color * direct;
    l.color = {c.x, c.y, c.z, 1.0f};
  } else {
    // Night: the moon, opposite the sun, pale and dim.
    vec3 moon = -p.sun;
    if (moon.y < 0.15f)
      moon = normalize(vec3{moon.x, 0.15f, moon.z});
    l.direction = -moon;
    const f32 k = smooth(0.03f, 0.2f, -e) * 0.2f * (1.0f - 0.7f * cover);
    l.color = {0.55f * k, 0.65f * k, 0.9f * k, 1.0f};
  }
  // Light from the whole sky: its average colour, less blue, more of it under
  // clouds (they spread the sun over the sky).
  vec3 a = (p.zenith * 0.55f + p.horizon * 0.45f);
  a = mix3(a, vec3{1.0f, 1.0f, 1.0f} * luminance(a), 0.45f) * lerp(0.9f, 1.25f, cover);
  a = a + vec3{0.012f, 0.014f, 0.022f};
  l.ambient = {std::min(a.x, 1.0f), std::min(a.y, 1.0f), std::min(a.z, 1.0f), 1.0f};
  l.fog_color = {p.fog_color.x, p.fog_color.y, p.fog_color.z, 1.0f};
  l.fog_density = std::max(base.fog_density, p.fog);
  return l;
}

void draw_sky3d(context &ctx, const sky3d &sky) {
  render3d_state &r = ctx.render3d;
  if (!r.active)
    return;
  world3d_store &w = ctx.world3d;
  w.sky = sky_params_of(sky);
  w.sky_drawn = true;
  w.wind = std::isfinite(sky.weather.wind.x) && std::isfinite(sky.weather.wind.y) ? sky.weather.wind : vec2{};
  w.wetness = clamp(sky.weather.wetness, 0.0f, 1.0f);
  if (sky.drive_light)
    light3d_set(ctx, sky3d_light(sky, light3d_get(ctx)));
  render3d_record_world(ctx, world3d_sky, 0);
  if (w.sky.rain > 0.0f || w.sky.snow > 0.0f)
    render3d_record_world(ctx, world3d_precip, 0);
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

namespace {
void advance(context &ctx) {
  ctx.world3d.time += delta(ctx);
  ctx.world3d.frame++;
}

void setup(context &ctx) { ecs_register(ctx, phase_update, advance, "clock"); }
} // namespace

mod_desc world3d_module() { return mod_desc{.name = "njin.world3d", .setup = setup}; }
} // namespace njin
