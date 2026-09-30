#include "render.h"
#include "audio.h"
#include "levels.h"
#include "sim.h"
#include "sprites.h"
#include "view.h"
#include "weather.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sandtable {

namespace {

// The world is a model sand table in 3D (view.h); the HUD, the flags' badges,
// the orders and the circle menu are drawn over it in screen pixels.

font_handle ui_font{};
constexpr f32 font_size = 16.0f;

// Instance data, rebuilt each frame (the terrain only when it changes). The
// engine's layout (draw_instanced3d): position and scale, colour, rotation in
// degrees, scale by axis. Round things come in thousands (heads, puffs,
// leaves), so they use the engine's low-poly meshes: a few pixels each on
// screen, and every triangle is drawn twice, for the shadows and the picture.
struct batch {
  instance_buffer_handle buffer{};
  std::vector<f32> data;
  u32 floats = 16;
  u32 count() const { return static_cast<u32>(data.size() / floats); }
};

batch frame{};  // the wooden frame round the sand
batch tree_trunks{};
batch tree_crowns{};
batch bodies{};  // soldiers: a cylinder...
batch heads{};   // ...and a sphere: a capsule
batch torches{}; // flames, glowing
batch corpses{};
batch marks{};   // scorches on the ground
batch arrows{};
batch shells{};
batch puffs{};   // smoke and dust, lit
batch glows{};   // fire and sparks, glowing
u32 built_terrain = ~0u;

void push(batch &b, vec3 pos, vec3 scale, rgba col, vec3 rot = {}) {
  b.data.insert(b.data.end(), {pos.x, pos.y, pos.z, 1.0f, col.r, col.g, col.b, col.a, rot.x, rot.y, rot.z, 0.0f,
                               scale.x, scale.y, scale.z, 0.0f});
}

void upload(context &ctx, batch &b) {
  if (b.buffer.id == 0)
    b.buffer = instance_buffer_create(ctx, b.floats);
  instance_buffer_upload(ctx, b.buffer, b.data.data(), b.count());
}

void draw(context &ctx, mesh3d_kind mesh, const batch &b) {
  if (b.buffer.id != 0 && b.count() > 0)
    draw_instanced3d(ctx, mesh, b.buffer, 0, b.count());
}

u32 hash2(u32 a, u32 b) {
  u32 h = (a * 0x9E3779B1u) ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  return h;
}
f32 unit(u32 a, u32 b) { return static_cast<f32>(hash2(a, b) & 0xFFFFu) / 65535.0f; }

rgba shade(rgba c, f32 k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
rgba with_alpha(rgba c, f32 a) { return {c.r, c.g, c.b, c.a * a}; }
rgba side_color(side s) { return s == side::player ? col_player : col_enemy; }
rgba side_dark(side s) { return s == side::player ? col_player_dark : col_enemy_dark; }

// Every eighth man carries a torch once it grows dark.
bool carries_torch(u32 index) { return index % 8 == 3; }

// A torch's flicker, 0.75 to 1, different for each torch.
f32 flicker(u32 index, f32 t) {
  return 0.85f + 0.1f * std::sin(t * 13.0f + static_cast<f32>(index) * 1.7f) +
         0.05f * std::sin(t * 31.0f + static_cast<f32>(index) * 0.3f);
}

// Where men and things stand: on the ground, or on the water over a river.
f32 stand_height(vec2 p) { return std::max(ground_height(p), water_level); }

// --- The table ---

constexpr f32 table_bottom = -1.2f; // the underside of the sand

// The sand is one smooth surface (view.h's height field), cut into square
// pieces. Each piece is made at a few levels of detail, every other sample
// dropped at each level, and the camera picks one by how far away the piece
// is: close ground gets every rise, the far side of the table a few
// triangles. Where two pieces of different detail meet, a skirt hangs from
// each piece's edge down into the table and hides the crack between them.
constexpr i32 chunk_tiles = 16;
constexpr i32 lod_count = 4;           // sample steps 1, 2, 4, 8
constexpr f32 lod_far[lod_count - 1] = {18.0f, 34.0f, 56.0f}; // 3D units to the camera

struct terrain_chunk {
  i32 i0 = 0, j0 = 0, ni = 0, nj = 0; // its samples in the height field
  vec3 middle{};
  model_handle lod[lod_count]{};
  // Its grass, in a shuffled order: drawing the first part of the list thins
  // the meadow evenly (draw_grass).
  instance_buffer_handle grass{};
  u32 grass_count = 0;
};
std::vector<terrain_chunk> chunks;

// --- Meadows ---
//
// Far away a meadow is only the green of the ground under it; close to the
// camera it grows tufts of grass. A tuft is a small model
// of a few blades (made once, model_create), drawn in thousands per piece
// of ground with one instanced draw, and never casts a shadow.

model_handle tuft{};
constexpr f32 grass_full = 16.0f; // 3D units to the camera: every tuft within this...
constexpr f32 grass_none = 30.0f; // ...fewer and fewer out to here, none beyond
constexpr i32 tufts_per_tile = 10;

// How much meadow there is at a table point, 0 to 1: patches of it over the
// open ground and the woods, none on hills, rock or water.
f32 meadow(vec2 p) {
  const i32 x = static_cast<i32>(std::floor(p.x / tile_world)), y = static_cast<i32>(std::floor(p.y / tile_world));
  const terrain t = terrain_cell(clamp(x, 0, tiles_x - 1), clamp(y, 0, tiles_y - 1));
  if (t != terrain::plain && t != terrain::forest)
    return 0.0f;
  const noise_desc patches{.seed = current_level().seed * 13u + 5u, .frequency = 0.09f, .octaves = 3};
  const f32 n = noise_2d(patches, p.x / tile_world, p.y / tile_world);
  const f32 k = clamp((n - 0.42f) / 0.16f, 0.0f, 1.0f);
  return t == terrain::forest ? std::max(k, 0.6f) : k;
}

// A tuft: blades round its foot, leaning out, dark at the root and light at
// the tip, both faces of each blade.
model_handle make_tuft(context &ctx) {
  std::vector<vec3> pos, nrm;
  std::vector<rgba> col;
  constexpr i32 blades = 7;
  const rgba root = rgb(46, 78, 34), tip = rgb(156, 186, 86);
  for (i32 b = 0; b < blades; ++b) {
    const f32 a = 2.0f * pi * (static_cast<f32>(b) + 0.35f * unit(static_cast<u32>(b), 3u)) / blades;
    const vec3 out{std::cos(a), 0.0f, std::sin(a)};
    const vec3 side{-out.z, 0.0f, out.x};
    const f32 tall = 0.14f + 0.12f * unit(static_cast<u32>(b), 9u);
    const vec3 foot = out * 0.015f;
    const vec3 l = foot - side * 0.012f, r = foot + side * 0.012f;
    const vec3 top = foot + out * (0.05f + 0.05f * unit(static_cast<u32>(b), 21u)) + vec3{0.0f, tall, 0.0f};
    for (const vec3 &v : {l, r, top, r, l, top}) {
      pos.push_back(v);
      nrm.push_back({0.0f, 1.0f, 0.0f}); // lit like the ground it grows on
      col.push_back(v.y > 0.01f ? tip : root);
    }
  }
  const model_handle m = model_create(ctx, {.positions = pos.data(),
                                            .normals = nrm.data(),
                                            .colors = col.data(),
                                            .vertex_count = static_cast<u32>(pos.size())});
  model_material mat = model_material_get(ctx, m, 0);
  mat.surface.specular = 0.0f;
  mat.surface.cast_shadows = false;
  model_material_set(ctx, m, -1, mat);
  return m;
}

// The grass of one piece of ground, shuffled.
void grow_meadow(context &ctx, terrain_chunk &c) {
  batch grass{};
  const i32 x0 = c.i0 / height_samples, y0 = c.j0 / height_samples;
  const i32 x1 = (c.i0 + c.ni) / height_samples, y1 = (c.j0 + c.nj) / height_samples;
  for (i32 y = y0; y < y1; ++y)
    for (i32 x = x0; x < x1; ++x)
      for (i32 k = 0; k < tufts_per_tile; ++k) {
        const u32 seed = static_cast<u32>(k) * 104729u + 31u;
        const vec2 at{(static_cast<f32>(x) + unit(static_cast<u32>(x) + seed, static_cast<u32>(y))) * tile_world,
                      (static_cast<f32>(y) + unit(static_cast<u32>(x), static_cast<u32>(y) + seed)) * tile_world};
        const f32 m = meadow(at);
        const f32 h = ground_height(at);
        if (h < water_level + 0.02f || unit(seed, static_cast<u32>(x * 131 + y)) > m)
          continue;
        const f32 size = 0.8f + 0.6f * unit(static_cast<u32>(y) + seed, static_cast<u32>(x));
        const f32 tone = 0.85f + 0.25f * unit(seed + 7u, static_cast<u32>(x + y * 64));
        push(grass, to3d(at, h - 0.01f), {size, size, size}, {tone, tone * 1.02f, tone * 0.9f, 1.0f},
             {0.0f, 360.0f * unit(seed + 3u, static_cast<u32>(y * 64 + x)), 0.0f});
      }
  // Shuffled, so the first part of the list is a thinner meadow all over.
  const auto shuffle = [](batch &b) {
    const u32 n = b.count();
    for (u32 i = n; i > 1; --i) {
      const u32 j = hash2(i, n) % i;
      for (u32 f = 0; f < b.floats; ++f)
        std::swap(b.data[(i - 1) * b.floats + f], b.data[j * b.floats + f]);
    }
  };
  shuffle(grass);
  c.grass_count = grass.count();
  if (c.grass_count > 0) {
    c.grass = instance_buffer_create(ctx, 16);
    instance_buffer_upload(ctx, c.grass, grass.data.data(), c.grass_count);
  }
}

// The colour of the ground at sample (i, j): the tiles' own colours blended
// across their borders, rock on steep slopes, pale on the high peaks, dark
// and wet under the water, and a little speckle.
rgba ground_color(const height_field &f, i32 i, i32 j) {
  const auto tile_color = [](i32 x, i32 y) {
    switch (terrain_cell(clamp(x, 0, tiles_x - 1), clamp(y, 0, tiles_y - 1))) {
    case terrain::forest:
      return rgb(150, 158, 98);
    case terrain::hill:
      return rgb(186, 152, 104);
    case terrain::mountain:
      return rgb(140, 128, 112);
    case terrain::river:
    case terrain::stream:
      return rgb(112, 118, 96);
    case terrain::ford:
      return rgb(188, 168, 126);
    default:
      return rgb(206, 180, 132);
    }
  };
  const f32 u = static_cast<f32>(i) / static_cast<f32>(height_samples) - 0.5f;
  const f32 v = static_cast<f32>(j) / static_cast<f32>(height_samples) - 0.5f;
  const i32 x0 = static_cast<i32>(std::floor(u)), y0 = static_cast<i32>(std::floor(v));
  const f32 fu = u - static_cast<f32>(x0), fv = v - static_cast<f32>(y0);
  rgba c = lerp(lerp(tile_color(x0, y0), tile_color(x0 + 1, y0), fu),
                lerp(tile_color(x0, y0 + 1), tile_color(x0 + 1, y0 + 1), fu), fv);
  const f32 h = f.at(i, j);
  const vec2 at{static_cast<f32>(i) / static_cast<f32>(height_samples) * tile_world,
                static_cast<f32>(j) / static_cast<f32>(height_samples) * tile_world};
  c = lerp(c, rgb(104, 138, 64), meadow(at) * 0.8f);
  const f32 steep = clamp((0.92f - f.normal_at(i, j).y) * 3.0f, 0.0f, 1.0f);
  c = lerp(c, rgb(118, 108, 96), steep);
  c = lerp(c, rgb(214, 208, 196), clamp((h - 1.4f) * 1.6f, 0.0f, 1.0f));
  if (h < water_level)
    c = lerp(c, rgb(70, 84, 78), clamp((water_level - h) * 3.0f, 0.0f, 0.8f));
  return shade(c, 0.95f + 0.1f * unit(static_cast<u32>(i), static_cast<u32>(j) + 5u));
}

// One piece at one level of detail: a grid of every `step`-th sample, and
// its skirt.
model_handle build_chunk(context &ctx, const height_field &f, const terrain_chunk &c, i32 step) {
  const i32 cols = c.ni / step + 1, rows = c.nj / step + 1;
  const f32 unit = 1.0f / static_cast<f32>(height_samples);
  std::vector<vec3> pos, nrm;
  std::vector<rgba> col;
  std::vector<u32> idx;
  const auto sample = [&](i32 a, i32 b, bool skirt) {
    const i32 i = c.i0 + a * step, j = c.j0 + b * step;
    pos.push_back({static_cast<f32>(i) * unit, skirt ? table_bottom : f.at(i, j), static_cast<f32>(j) * unit});
    nrm.push_back(f.normal_at(i, j));
    col.push_back(skirt ? shade(ground_color(f, i, j), 0.7f) : ground_color(f, i, j));
    return static_cast<u32>(pos.size() - 1);
  };
  for (i32 b = 0; b < rows; ++b)
    for (i32 a = 0; a < cols; ++a)
      sample(a, b, false);
  const auto at = [&](i32 a, i32 b) { return static_cast<u32>(b * cols + a); };
  for (i32 b = 0; b + 1 < rows; ++b)
    for (i32 a = 0; a + 1 < cols; ++a) {
      idx.insert(idx.end(), {at(a, b), at(a, b + 1), at(a + 1, b)});
      idx.insert(idx.end(), {at(a + 1, b), at(a, b + 1), at(a + 1, b + 1)});
    }
  // The skirt, both faces, along the four edges.
  const auto skirt = [&](i32 a0, i32 b0, i32 da, i32 db, i32 n) {
    for (i32 k = 0; k + 1 < n; ++k) {
      const i32 a = a0 + da * k, b = b0 + db * k;
      const u32 top0 = at(a, b), top1 = at(a + da, b + db);
      const u32 low0 = sample(a, b, true), low1 = sample(a + da, b + db, true);
      idx.insert(idx.end(), {top0, low0, top1, top1, low0, low1});
      idx.insert(idx.end(), {top0, top1, low0, top1, low1, low0});
    }
  };
  skirt(0, 0, 1, 0, cols);
  skirt(0, rows - 1, 1, 0, cols);
  skirt(0, 0, 0, 1, rows);
  skirt(cols - 1, 0, 0, 1, rows);
  const model_handle m = model_create(ctx, {.positions = pos.data(),
                                            .normals = nrm.data(),
                                            .colors = col.data(),
                                            .vertex_count = static_cast<u32>(pos.size()),
                                            .indices = idx.data(),
                                            .index_count = static_cast<u32>(idx.size())});
  model_material mat = model_material_get(ctx, m, 0);
  mat.surface.specular = 0.06f;
  mat.surface.shininess = 10.0f;
  model_material_set(ctx, m, -1, mat);
  return m;
}

void free_chunks(context &ctx) {
  for (terrain_chunk &c : chunks) {
    for (model_handle &m : c.lod)
      if (m.id != 0) {
        model_unload(ctx, m);
        m = {};
      }
    if (c.grass.id != 0) {
      instance_buffer_destroy(ctx, c.grass);
      c.grass = {};
    }
  }
  chunks.clear();
}

// The ground, the woods and the frame: built once for each terrain (levels.h).
void build_table(context &ctx) {
  free_chunks(ctx);
  const height_field &f = ground_field();
  const i32 span = chunk_tiles * height_samples;
  for (i32 j0 = 0; j0 < f.depth - 1; j0 += span)
    for (i32 i0 = 0; i0 < f.width - 1; i0 += span) {
      terrain_chunk c{};
      c.i0 = i0;
      c.j0 = j0;
      c.ni = std::min(span, f.width - 1 - i0);
      c.nj = std::min(span, f.depth - 1 - j0);
      const f32 unit = 1.0f / static_cast<f32>(height_samples);
      c.middle = {(static_cast<f32>(i0) + static_cast<f32>(c.ni) * 0.5f) * unit, 0.0f,
                  (static_cast<f32>(j0) + static_cast<f32>(c.nj) * 0.5f) * unit};
      for (i32 l = 0; l < lod_count; ++l)
        c.lod[l] = build_chunk(ctx, f, c, 1 << l);
      grow_meadow(ctx, c);
      chunks.push_back(c);
    }

  for (batch *b : {&frame, &tree_trunks, &tree_crowns})
    b->data.clear();
  const rgba wood = rgb(92, 66, 44), leaves = rgb(72, 108, 58);
  for (i32 y = 0; y < tiles_y; ++y)
    for (i32 x = 0; x < tiles_x; ++x) {
      if (terrain_cell(x, y) != terrain::forest)
        continue;
      const i32 n = 1 + static_cast<i32>(unit(static_cast<u32>(x), static_cast<u32>(y) + 999u) * 2.99f);
      for (i32 k = 0; k < n; ++k) {
        const u32 seed = static_cast<u32>(k) * 7919u + 17u;
        const f32 ox = 0.2f + 0.6f * unit(static_cast<u32>(x) + seed, static_cast<u32>(y));
        const f32 oz = 0.2f + 0.6f * unit(static_cast<u32>(x), static_cast<u32>(y) + seed);
        const f32 r = 0.2f + 0.14f * unit(static_cast<u32>(x) + seed, static_cast<u32>(y) + seed);
        const vec2 at{(static_cast<f32>(x) + ox) * tile_world, (static_cast<f32>(y) + oz) * tile_world};
        const vec3 foot = to3d(at, ground_height(at) - 0.03f);
        push(tree_trunks, foot, {0.05f, 0.33f, 0.05f}, wood);
        push(tree_crowns, foot + vec3{0.0f, 0.33f + r * 0.7f, 0.0f}, {r, r * 1.2f, r},
             shade(leaves, 0.85f + 0.3f * unit(seed, static_cast<u32>(x + y))));
      }
    }
  // The wooden frame round the sand, its top edge lighter.
  const f32 w = static_cast<f32>(tiles_x), h = static_cast<f32>(tiles_y);
  const f32 rim = 0.8f, frame_top = 0.25f, frame_h = frame_top - table_bottom - 0.3f;
  const f32 fy = frame_top - frame_h * 0.5f;
  push(frame, {w * 0.5f, fy, -rim * 0.5f}, {w + rim * 2.0f, frame_h, rim}, col_frame);
  push(frame, {w * 0.5f, fy, h + rim * 0.5f}, {w + rim * 2.0f, frame_h, rim}, col_frame);
  push(frame, {-rim * 0.5f, fy, h * 0.5f}, {rim, frame_h, h}, col_frame);
  push(frame, {w + rim * 0.5f, fy, h * 0.5f}, {rim, frame_h, h}, col_frame);
  for (i32 i = 0; i < 4; ++i) {
    const bool along = i < 2;
    const vec3 c = along ? vec3{w * 0.5f, frame_top + 0.02f, i == 0 ? -rim * 0.5f : h + rim * 0.5f}
                         : vec3{i == 2 ? -rim * 0.5f : w + rim * 0.5f, frame_top + 0.02f, h * 0.5f};
    push(frame, c, along ? vec3{w + rim * 2.0f, 0.04f, rim} : vec3{rim, 0.04f, h}, col_frame_light);
  }
  for (batch *b : {&frame, &tree_trunks, &tree_crowns})
    upload(ctx, *b);
}

// Each piece of ground at the detail its distance to the camera asks for.
void draw_ground(context &ctx, const camera3d &cam) {
  for (const terrain_chunk &c : chunks) {
    const f32 d = distance(cam.position, c.middle);
    i32 l = 0;
    while (l < lod_count - 1 && d > lod_far[l])
      ++l;
    draw_model(ctx, c.lod[l], {});
  }
}

// The meadows close to the camera: all of a piece's tufts near it, fewer
// further out, none far off, and none behind the camera.
void draw_grass(context &ctx, const camera3d &cam) {
  if (tuft.id == 0)
    return;
  const vec3 ahead = normalize(cam.target - cam.position);
  const f32 reach = static_cast<f32>(chunk_tiles) * 0.75f; // a piece's half diagonal, near enough
  material3d_set(ctx, {.specular = 0.0f, .cast_shadows = false});
  for (const terrain_chunk &c : chunks) {
    const vec3 to = c.middle - cam.position;
    if (dot(to, ahead) < -reach)
      continue;
    const f32 d = std::max(0.0f, length(to) - reach);
    const f32 k = clamp((grass_none - d) / (grass_none - grass_full), 0.0f, 1.0f);
    const u32 n = static_cast<u32>(static_cast<f32>(c.grass_count) * k * k);
    if (n > 0)
      draw_instanced3d(ctx, tuft, c.grass, 0, n);
  }
  material3d_set(ctx, {});
}

// The home band and the enemy camp, as coloured sheets over the sand.
void draw_zones(context &ctx) {
  const f32 a = state.screen == phase::deploy ? 1.0f : 0.4f;
  material3d_set(ctx, {.specular = 0.0f, .cast_shadows = false});
  const auto sheet = [&](const rect &z, rgba col) {
    draw_plane3d(ctx, to3d(z.pos + z.size * 0.5f, 0.012f), z.size * unit3d, with_alpha(col, a));
  };
  sheet(player_zone, rgb(205, 52, 44, 40));
  sheet(enemy_zone, rgb(36, 110, 160, 40));
  material3d_set(ctx, {});
}

// --- Flags (setting up) ---

constexpr f32 pole_height = 1.1f;

// A troop's flag planted at its place: a pole, and a cloth of the colour of
// its size with a band of its side's colour. `lit` glows gold.
void draw_flag3d(context &ctx, vec2 at, i32 tier, side owner, bool lit, f32 alpha = 1.0f, bool bad = false) {
  const vec3 foot = to3d(at, stand_height(at));
  const vec3 top = foot + vec3{0.0f, pole_height, 0.0f};
  material3d_set(ctx, {.specular = 0.2f, .emission = lit ? rgba{1.0f, 0.8f, 0.3f, 0.35f} : rgba{}});
  draw_cylinder3d(ctx, foot, top, 0.03f, with_alpha(rgb(70, 52, 34), alpha));
  draw_sphere3d(ctx, top, 0.05f, with_alpha(col_gold, alpha));
  // The cloth turns to face the camera, so it reads from any side.
  const f32 yaw = state.cam_yaw * pi / 180.0f;
  const vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
  const rgba cloth = bad ? col_bad : tiers[tier].color;
  const vec3 mid = top + right * 0.26f + vec3{0.0f, -0.18f, 0.0f};
  const vec3 size{std::fabs(right.x) * 0.48f + 0.02f, 0.3f, std::fabs(right.z) * 0.48f + 0.02f};
  draw_cube3d(ctx, mid, size, with_alpha(cloth, alpha));
  draw_cube3d(ctx, mid + vec3{0.0f, -0.17f, 0.0f}, {size.x, 0.05f, size.z}, with_alpha(side_color(owner), alpha));
  material3d_set(ctx, {});
}

// --- Battle ---

void draw_soldiers(context &ctx) {
  bodies.data.clear();
  heads.data.clear();
  torches.data.clear();
  const bool dark = darkness() > 0.3f;
  const f32 t = elapsed(ctx);
  for (u32 i = 0; i < state.soldiers.size(); ++i) {
    const soldier &s = state.soldiers[i];
    if (!s.alive)
      continue;
    const f32 r = s.radius * unit3d * 0.75f;
    const f32 tall = r * 3.2f;
    f32 base = stand_height(s.pos);
    if (s.moving)
      base += std::fabs(std::sin(s.anim * 12.0f)) * r * 0.35f; // a step
    const vec3 foot = to3d(s.pos, base);
    const bool flash = s.flash > 0.0f;
    const rgba body = flash ? col_white : side_color(s.owner);
    const rgba head = flash ? col_white : (s.owner == side::player ? col_player_light : col_enemy_light);
    push(bodies, foot, {r, tall, r}, body);
    push(heads, foot + vec3{0.0f, tall, 0.0f}, {r, r, r}, head);
    if (dark && carries_torch(i)) {
      const vec3 hand = foot + vec3{s.facing.x * r * 1.3f, tall + r * 1.4f, s.facing.y * r * 1.3f};
      const f32 f = flicker(i, t);
      push(torches, hand, vec3{r, r * 1.5f, r} * (0.28f * f), rgb(255, static_cast<i32>(120 + 80 * f), 40));
    }
  }
  upload(ctx, bodies);
  upload(ctx, heads);
  upload(ctx, torches);
  material3d_set(ctx, {.specular = 0.45f, .shininess = 40.0f, .rim = {1.0f, 1.0f, 1.0f, 0.15f}});
  draw(ctx, mesh3d_cylinder_low, bodies);
  draw(ctx, mesh3d_sphere_low, heads);
  material3d_set(ctx, {.emission = {1.0f, 0.6f, 0.2f, 1.0f}, .unlit = true, .cast_shadows = false});
  draw(ctx, mesh3d_sphere_low, torches);
  material3d_set(ctx, {});
}

// Colour of a particle at `k` = life left, 1 new to 0 gone.
rgba particle_color(const fx_particle &p, f32 k) {
  switch (p.kind) {
  case fx_kind::fire:
    if (k > 0.8f)
      return rgb(255, 250, 220);
    if (k > 0.55f)
      return rgb(255, 214, 90);
    if (k > 0.3f)
      return rgb(240, 130, 40);
    return rgb(170, 60, 30, 220);
  case fx_kind::smoke:
    return with_alpha(p.color, std::min(1.0f, k * 1.6f) * 0.6f);
  case fx_kind::dust:
    return with_alpha(p.color, k * 0.8f);
  default:
    return with_alpha(p.color, std::min(1.0f, k * 2.0f));
  }
}

// The sim's particles live on the table; in 3D smoke and fire also rise.
void draw_particles(context &ctx) {
  puffs.data.clear();
  glows.data.clear();
  for (const fx_particle &p : state.particles) {
    if (p.delay > 0.0f || p.kind == fx_kind::ring)
      continue;
    const f32 k = clamp(p.life / std::max(0.001f, p.max_life), 0.0f, 1.0f);
    const f32 age = 1.0f - k;
    const rgba col = particle_color(p, k);
    f32 lift = 0.12f, size = p.size * unit3d * 0.5f;
    switch (p.kind) {
    case fx_kind::fire:
      lift = 0.1f + age * 0.5f;
      size *= 0.4f + 0.6f * k;
      break;
    case fx_kind::smoke:
      lift = 0.3f + age * 1.4f;
      break;
    case fx_kind::dust:
      lift = 0.05f + age * 0.25f;
      break;
    case fx_kind::debris:
      lift = 0.05f + std::sin(age * pi) * 0.6f;
      break;
    default:
      lift = 0.2f + age * 0.2f;
      break;
    }
    const vec3 at = to3d(p.pos, stand_height(p.pos) + lift);
    const bool glow = p.kind == fx_kind::fire || p.kind == fx_kind::spark;
    push(glow ? glows : puffs, at, {size, size, size}, col);
  }
  upload(ctx, puffs);
  upload(ctx, glows);
  material3d_set(ctx, {.specular = 0.0f, .cast_shadows = false});
  draw(ctx, mesh3d_sphere_low, puffs);
  material3d_set(ctx, {.emission = {1.0f, 0.7f, 0.3f, 0.8f}, .unlit = true, .cast_shadows = false});
  draw(ctx, mesh3d_sphere_low, glows);
  material3d_set(ctx, {});
}

// Degrees that turn the cylinder mesh (up +y) to point along `d`.
vec3 turn_to(vec3 d) {
  const f32 len = length(d);
  if (len < 0.0001f)
    return {};
  const f32 pitch = std::acos(clamp(d.y / len, -1.0f, 1.0f));
  const f32 yaw = std::atan2(d.x, d.z);
  return {pitch * 180.0f / pi, yaw * 180.0f / pi, 0.0f};
}

void draw_battle(context &ctx) {
  // Scorches, the dead, then the living.
  marks.data.clear();
  for (const scorch &sc : state.scorches) {
    const f32 r = sc.radius * unit3d;
    push(marks, to3d(sc.pos, stand_height(sc.pos) + 0.004f), {r, 0.01f, r}, rgb(60, 48, 36, 190));
  }
  upload(ctx, marks);
  material3d_set(ctx, {.specular = 0.0f, .cast_shadows = false});
  draw(ctx, mesh3d_cylinder_low, marks);
  material3d_set(ctx, {});

  corpses.data.clear();
  for (const corpse &c : state.corpses) {
    const f32 r = c.radius * unit3d * 0.8f;
    const vec3 foot = to3d(c.pos, stand_height(c.pos) + r);
    const vec3 along{c.facing.x, 0.0f, c.facing.y};
    push(corpses, foot - along * (r * 1.6f), {r, r * 3.2f, r}, shade(side_dark(c.owner), 0.8f), turn_to(along));
  }
  upload(ctx, corpses);
  material3d_set(ctx, {.specular = 0.1f});
  draw(ctx, mesh3d_cylinder_low, corpses);
  material3d_set(ctx, {});

  draw_soldiers(ctx);

  arrows.data.clear();
  shells.data.clear();
  for (const projectile &p : state.projectiles) {
    const auto at = [&](f32 t) {
      const vec2 ground = lerp(p.from, p.to, t);
      return to3d(ground, stand_height(ground) + 0.35f + std::sin(t * pi) * p.arc * unit3d);
    };
    const vec3 here = at(p.t);
    if (p.source == arm::artillery) {
      push(shells, here, {0.07f, 0.07f, 0.07f}, rgb(40, 38, 36));
    } else {
      // A shaft along its flight, its tip where the arrow is.
      const vec3 d = at(std::min(1.0f, p.t + 0.03f)) - here;
      const vec3 way = length(d) > 0.0001f ? normalize(d) : vec3{0.0f, 1.0f, 0.0f};
      push(arrows, here - way * 0.24f, {0.012f, 0.24f, 0.012f}, rgb(70, 52, 34), turn_to(way));
    }
  }
  upload(ctx, arrows);
  upload(ctx, shells);
  draw(ctx, mesh3d_cylinder_low, arrows);
  draw(ctx, mesh3d_sphere_low, shells);

  draw_particles(ctx);
}

// --- Light ---

// The sun follows the hour across the sky; at night the moon stands in, low
// and blue. Torches, camp fires and blasts are lights of their own.
void set_light(context &ctx) {
  const vec3 sky = daylight();
  const f32 h = state.hour;
  const bool day = h > 5.5f && h < 19.0f;
  const f32 a = day ? (h - 5.5f) / 13.5f * pi : 0.8f;
  const vec3 dir{-std::cos(a) * 0.8f, -std::max(0.35f, std::sin(a)), -0.45f};
  const f32 dark = darkness();
  light3d_set(ctx, {.direction = dir,
                    .color = {sky.x * sky.x * 0.95f, sky.y * sky.y * 0.9f, sky.z * sky.z * 0.85f, 1.0f},
                    // Dim at night, so the torches and fires carry the scene.
                    .ambient = {0.06f + sky.x * 0.34f, 0.06f + sky.y * 0.34f, 0.1f + sky.z * 0.36f, 1.0f},
                    .shadows = true,
                    .shadow_range = clamp(state.cam_distance * 0.75f, 12.0f, 48.0f),
                    .shadow_size = 2048,
                    .shadow_softness = 1.5f,
                    .fog_color = {0.06f + 0.1f * (1.0f - dark), 0.06f + 0.1f * (1.0f - dark),
                                  0.08f + 0.12f * (1.0f - dark), 1.0f},
                    .fog_density = 0.006f});
}

void add_lights(context &ctx) {
  const f32 dark = darkness();
  if (dark < 0.05f)
    return;
  const f32 t = elapsed(ctx);
  i32 left = light3d_max;
  const auto add = [&](vec2 at, f32 lift, f32 radius, f32 strength, rgba col) {
    if (left <= 0)
      return;
    --left;
    light3d_add(ctx, {.position = to3d(at, stand_height(at) + lift), .color = col, .intensity = strength,
                      .radius = radius});
  };
  const rgba fire = rgb(255, 170, 90);
  // Blasts first, only their big flashes.
  for (const fx_particle &p : state.particles)
    if (p.kind == fx_kind::fire && p.delay <= 0.0f && p.size >= 10.0f && left > light3d_max / 2)
      add(p.pos, 0.5f, p.size * unit3d * 8.0f, 2.0f * dark * clamp(p.life / p.max_life * 2.0f, 0.0f, 1.0f), fire);
  if (state.screen == phase::deploy) {
    // Camp fires: at home for the player, at the camps for the enemy.
    for (const troop &c : state.board) {
      const vec2 home = troop_home(c);
      add(home, 0.6f, 5.0f, 1.4f * dark * flicker(static_cast<u32>(home.x), t), fire);
    }
    for (const troop &c : current_level().enemy)
      add(c.pos, 0.6f, 5.0f, 1.4f * dark * flicker(static_cast<u32>(c.pos.x), t), fire);
    return;
  }
  if (dark < 0.3f)
    return;
  // Torches: more than there are lights, so every so many, across the field.
  i32 count = 0;
  for (u32 i = 0; i < state.soldiers.size(); ++i)
    count += state.soldiers[i].alive && carries_torch(i) ? 1 : 0;
  const i32 stride = std::max(1, (count + left - 1) / std::max(1, left));
  i32 seen = 0;
  for (u32 i = 0; i < state.soldiers.size(); ++i) {
    const soldier &s = state.soldiers[i];
    if (!s.alive || !carries_torch(i) || seen++ % stride != 0)
      continue;
    add(s.pos, 0.6f, 3.0f + std::min(4.0f, static_cast<f32>(stride) * 0.3f), 1.3f * dark * flicker(i, t), fire);
  }
}

// --- Over the picture: text and marks in screen pixels ---

void text(context &ctx, const char *str, vec2 pos, rgba col, f32 size = font_size) {
  draw_text(ctx, str, {std::floor(pos.x), std::floor(pos.y)}, size, col, ui_font);
}
vec2 measure(context &ctx, const char *str, f32 size = font_size) { return text_measure(ctx, str, size, ui_font); }
void text_centered(context &ctx, const char *str, vec2 center, rgba col, f32 size = font_size) {
  text(ctx, str, center - measure(ctx, str, size) * 0.5f, col, size);
}
// Text with a one-pixel dark shadow, for text over the table.
void text_shadow(context &ctx, const char *str, vec2 pos, rgba col, f32 size = font_size) {
  text(ctx, str, pos + vec2{1.0f, 1.0f}, rgb(20, 14, 10, static_cast<i32>(200 * col.a)), size);
  text(ctx, str, pos, col, size);
}

// An arrow in screen pixels from `from` along `dir`, with a two-stroke head.
void draw_arrow(context &ctx, vec2 from, vec2 dir, f32 len, rgba col) {
  const vec2 tip = from + dir * len;
  const vec2 side{-dir.y, dir.x};
  const f32 head = std::min(6.0f, len * 0.4f);
  draw_line(ctx, from, tip, 1.0f, col);
  draw_line(ctx, tip, tip - dir * head + side * head * 0.7f, 1.0f, col);
  draw_line(ctx, tip, tip - dir * head - side * head * 0.7f, 1.0f, col);
}

// A troop's badge over the picture, its bottom middle at `at` (screen): the
// colour of its size, its arm's symbol, and a bar of how many are left.
void draw_badge(context &ctx, vec2 at, arm a, i32 tier, side owner, f32 left, rgba edge) {
  const rect flag{{std::floor(at.x) - 8.0f, std::floor(at.y) - 14.0f}, {17.0f, 11.0f}};
  draw_rect(ctx, flag, tiers[tier].color);
  draw_rect_lines(ctx, flag, 1.0f, edge);
  // Dark or light to stand out on the size colour.
  const bool dark_flag = tier == 3 || tier == 4;
  draw_arm_symbol(ctx, flag.pos + vec2{6.0f, 3.0f}, a, 1.0f, dark_flag ? col_white : rgb(30, 26, 22));
  draw_rect(ctx, {flag.pos + vec2{0.0f, 12.0f}, {17.0f, 2.0f}}, rgb(20, 14, 10, 160));
  draw_rect(ctx, {flag.pos + vec2{0.0f, 12.0f}, {std::ceil(17.0f * left), 2.0f}}, side_color(owner));
}

// Where a flag's badge sits on screen: over the top of its pole.
vec2 badge_point(context &ctx, vec2 at) { return table_to_screen(ctx, at, pole_height + 0.15f); }

// A troop's orders over the picture: a dashed way from home to its flag, a
// ring at the flag and an arrow the way it attacks, laid along the table.
void draw_orders(context &ctx, vec2 home, vec2 post, vec2 face, rgba col) {
  const vec2 a = table_to_screen(ctx, home);
  const vec2 b = table_to_screen(ctx, post);
  const f32 len = distance(a, b);
  if (len > 8.0f) {
    const vec2 dir = (b - a) / len;
    for (f32 d = 0.0f; d < len - 4.0f; d += 6.0f)
      draw_line(ctx, a + dir * d, a + dir * std::min(len - 4.0f, d + 3.0f), 1.0f, col);
  }
  draw_circle_lines(ctx, b, 4.0f, 1.0f, col);
  // The arrow runs along the ground the way the troop faces, as the camera
  // sees that way.
  const vec2 tip = table_to_screen(ctx, post + face * 60.0f);
  if (distance(tip, b) > 1.0f) {
    const vec2 way = normalize(tip - b);
    draw_arrow(ctx, b + way * 6.0f, way, 16.0f, col);
  }
}

// Which way troop `i` faces at its flag, as shown: its orders, or the way the
// mouse is giving it now.
vec2 order_face(context &ctx, i32 i) {
  const troop &c = state.board[static_cast<usize>(i)];
  if (i != state.selected || state.cmd != command::face)
    return c.face;
  vec2 at{};
  if (!mouse_on_table(ctx, &at))
    return c.face;
  const vec2 drag = at - c.pos;
  return distance(mouse_pos(ctx), table_to_screen(ctx, c.pos)) >= 6.0f && length(drag) > 0.001f ? normalize(drag)
                                                                                                  : c.face;
}

void draw_overlays(context &ctx) {
  if (state.screen == phase::deploy) {
    const bool pointing = !ui_mouse_over(ctx) && state.cmd == command::none && state.menu.kind == menu_kind::none;
    const i32 hover = pointing ? flag_at(ctx, side::player) : -1;
    for (usize i = 0; i < state.board.size(); ++i) {
      const troop &c = state.board[i];
      const i32 k = static_cast<i32>(i);
      const bool selected = k == state.selected && state.cmd != command::none;
      draw_orders(ctx, troop_home(c), c.pos, order_face(ctx, k),
                  selected ? col_gold_light : with_alpha(col_player_light, 0.8f));
    }
    for (const troop &c : current_level().enemy)
      draw_badge(ctx, badge_point(ctx, c.pos), c.type, c.tier, c.owner, 1.0f, side_color(c.owner));
    for (usize i = 0; i < state.board.size(); ++i) {
      const troop &c = state.board[i];
      const i32 k = static_cast<i32>(i);
      const bool lit = k == hover || k == state.selected || k == state.menu.troop;
      draw_badge(ctx, badge_point(ctx, c.pos), c.type, c.tier, c.owner, 1.0f, lit ? col_gold_light : side_color(c.owner));
    }
    // At each zone's corner, kept on screen.
    const vec2 scr = screen_size(ctx);
    const auto corner = [&](const rect &z) {
      const vec2 p = table_to_screen(ctx, z.pos) + vec2{4.0f, 2.0f};
      return vec2{clamp(p.x, 4.0f, scr.x - 80.0f), clamp(p.y, 52.0f, scr.y - 40.0f)};
    };
    text_shadow(ctx, "NHÀ", corner(player_zone), rgb(240, 150, 130));
    text_shadow(ctx, "QUÂN ĐỊCH", corner(enemy_zone), rgb(150, 200, 240));
    return;
  }
  // Where each of the player's blocks was told to hold, and which way.
  for (const group &g : state.groups)
    if (g.garrison && g.alive > 0)
      draw_orders(ctx, g.post, g.post, g.face, with_alpha(col_player_light, 0.5f));
  // A badge over each block: size colour, arm, and how many are left.
  for (const group &g : state.groups) {
    if (g.alive == 0)
      continue;
    const f32 left = static_cast<f32>(g.alive) / static_cast<f32>(std::max(1, g.figures));
    bool visible = false;
    const vec2 at = table_to_screen(ctx, g.centroid, 0.9f, &visible);
    if (visible)
      draw_badge(ctx, at, g.type, g.tier, g.owner, left, side_color(g.owner));
  }
  for (const popup_text &p : state.popups) {
    const f32 a = clamp(p.timer / std::max(0.001f, p.max_time), 0.0f, 1.0f);
    const vec2 at = table_to_screen(ctx, p.pos, 1.5f);
    text_centered(ctx, p.label.c_str(), at + vec2{2.0f, 2.0f}, rgb(20, 14, 10, static_cast<i32>(200 * a)), 32.0f);
    text_centered(ctx, p.label.c_str(), at, with_alpha(p.color, a), 32.0f);
  }
}

// --- Field ledger: paper, ink and a single vermilion command ---

constexpr rgba col_panel = rgb(26, 31, 29, 235);
constexpr rgba col_panel_edge = rgb(78, 85, 73);
constexpr rgba col_button = rgb(42, 48, 43);
constexpr rgba col_button_hover = rgb(66, 75, 62);
constexpr rgba col_button_down = rgb(91, 103, 83);
constexpr rgba col_button_off = rgb(31, 36, 32);
constexpr rgba col_text_hint = rgb(164, 172, 151);
constexpr rgba col_paper = rgb(218, 210, 184);
constexpr rgba col_seal = rgb(148, 54, 41);
ui_style hud_style{};

ui_skin flat(rgba color, rgba edge = rgb(20, 14, 10)) {
  return ui_skin{.color = color, .roundness = 0.0f, .outline = edge, .outline_width = 1.0f};
}

ui_style make_hud_style() {
  ui_style s = ui_default_style();
  s.font = ui_font;
  s.font_size = font_size;
  s.padding = 8.0f;
  s.spacing = 2.0f;
  s.widget_height = 20.0f;
  s.width = 200.0f;
  s.toast_width = 240.0f;
  s.toast_margin = {8.0f, 72.0f};
  s.toast.normal = flat(col_panel, col_panel_edge);
  s.toast.text = col_paper;
  s.toast_accent[0] = col_text_hint;
  s.toast_accent[1] = rgb(111, 139, 94);
  s.toast_accent[2] = rgb(188, 153, 87);
  s.toast_accent[3] = col_seal;
  s.dim = rgb(0, 0, 0, 150);

  s.panel.normal = flat(col_panel, col_panel_edge);
  s.panel.text = col_paper;
  s.label.text = s.label.text_focused = col_white;

  s.button.normal = flat(col_button);
  s.button.focused = flat(col_button_hover, col_panel_edge);
  s.button.pressed = flat(col_button_down, col_paper);
  s.button.disabled = flat(col_button_off, rgb(30, 24, 18));
  s.button.text = col_white;
  s.button.text_focused = col_paper;
  s.button.text_disabled = rgb(110, 100, 86);

  const ui_skin slot = flat(rgb(20, 16, 12));
  s.track.normal = s.track.focused = s.track.pressed = s.track.disabled = slot;
  s.track.text = s.track.text_focused = col_white;
  const ui_skin fill{.color = col_good};
  s.fill.normal = s.fill.focused = s.fill.pressed = s.fill.disabled = fill;
  for (ui_look *look : {&s.panel, &s.button, &s.track, &s.fill, &s.knob, &s.toast})
    for (ui_skin *skin : {&look->normal, &look->focused, &look->pressed, &look->disabled})
      skin->roundness = 0.0f;

  s.sound_move = {};
  s.sound_accept = audio_sound(sfx_type::click);
  return s;
}

void hud_label(context &ctx, const char *str, rgba color) {
  ui_style s = ui_style_get(ctx);
  const rgba old = s.label.text;
  s.label.text = color;
  ui_style_set(ctx, s);
  ui_label(ctx, str);
  s.label.text = old;
  ui_style_set(ctx, s);
}

void hud_bar(context &ctx, f32 value, const char *str, rgba color) {
  ui_style s = ui_style_get(ctx);
  const ui_look old = s.fill;
  s.fill.normal.color = color;
  ui_style_set(ctx, s);
  ui_progress(ctx, value, str);
  s.fill = old;
  ui_style_set(ctx, s);
}

void format_men(char *buf, usize n, f32 men) {
  if (men >= 10000.0f)
    std::snprintf(buf, n, "%.1fK", men / 1000.0f);
  else
    std::snprintf(buf, n, "%.0f", men);
}

// A selected tab is inked in, rather than marked with a text prefix.
bool hud_button(context &ctx, const char *label, bool selected = false, bool enabled = true) {
  const ui_style old = ui_style_get(ctx);
  if (selected) {
    ui_style s = old;
    s.button.normal = flat(col_seal, col_seal);
    s.button.focused = flat(rgb(171, 67, 49), col_seal);
    s.button.pressed = flat(rgb(116, 41, 33), col_seal);
    s.button.text = s.button.text_focused = col_paper;
    ui_style_set(ctx, s);
  }
  const bool clicked = ui_button(ctx, label, enabled);
  ui_style_set(ctx, old);
  return clicked;
}

void top_bar(context &ctx) {
  const vec2 scr = screen_size(ctx);
  const level_def &lvl = current_level();
  ui_begin(ctx, {.id = "top_bar", .anchor = {0.0f, 0.0f}, .pivot = {0.0f, 0.0f},
                 .width = scr.x, .navigable = false});
  char title[128];
  std::snprintf(title, sizeof(title), "%s", lvl.name);
  hud_label(ctx, title, col_paper);
  ui_row(ctx, 3);
  char info[80];
  if (state.screen == phase::deploy) {
    std::snprintf(info, sizeof(info), "Đơn vị  %d", troop_count(side::player));
    hud_label(ctx, info, col_paper);
    std::snprintf(info, sizeof(info), "Quân số mới  %s %s", tiers[state.new_tier].name, tiers[state.new_tier].value);
    hud_label(ctx, info, col_text_hint);
    std::snprintf(info, sizeof(info), "%02d:00  /  Bày trận", static_cast<i32>(state.hour));
    hud_label(ctx, info, col_text_hint);
  } else {
    char now[24], start[24];
    for (i32 i = 0; i < 2; ++i) {
      format_men(now, sizeof(now), state.men_now[i]);
      format_men(start, sizeof(start), state.men_start[i]);
      std::snprintf(info, sizeof(info), "%s  %s / %s", i == 0 ? "Ta" : "Địch", now, start);
      hud_bar(ctx, state.men_now[i] / std::max(1.0f, state.men_start[i]), info,
              i == 0 ? rgb(141, 63, 49) : rgb(50, 91, 106));
    }
    const i32 left = static_cast<i32>(std::max(0.0f, battle_time_limit - state.battle_time));
    std::snprintf(info, sizeof(info), "%d:%02d  /  %s", left / 60, left % 60,
                  time_paused(ctx) ? "Tạm dừng" : "Giao chiến");
    hud_label(ctx, info, left < 30 ? col_bad : col_paper);
  }
  ui_end(ctx);
}

void deploy_bar(context &ctx) {
  ui_begin(ctx, {.id = "deploy_nav", .anchor = {0.0f, 1.0f}, .pivot = {0.0f, 1.0f},
                 .offset = {8.0f, -6.0f}, .width = 90.0f, .navigable = false});
  if (ui_button(ctx, "Dọn bàn", !state.board.empty()))
    clear_board(ctx);
  ui_end(ctx);
  ui_begin(ctx, {.id = "deploy_order", .anchor = {1.0f, 1.0f}, .pivot = {1.0f, 1.0f},
                 .offset = {-8.0f, -6.0f}, .width = 136.0f, .navigable = false});
  if (hud_button(ctx, "XUẤT QUÂN", true, !state.board.empty()))
    start_battle(ctx);
  ui_end(ctx);
}

void battle_bar(context &ctx) {
  ui_begin(ctx, {.id = "battle_bar", .anchor = {0.5f, 1.0f}, .pivot = {0.5f, 1.0f}, .offset = {0.0f, -6.0f},
                 .width = 220.0f, .navigable = false});
  ui_row(ctx, 4);
  if (hud_button(ctx, time_paused(ctx) ? "Chạy" : "Dừng", time_paused(ctx)))
    time_set_paused(ctx, !time_paused(ctx));
  const char *labels[3] = {"x1", "x2", "x4"};
  for (i32 i = 0; i < 3; ++i)
    if (hud_button(ctx, labels[i], i == state.speed_index))
      set_speed(ctx, i);
  ui_end(ctx);
}

// A small framed box of text lines at the mouse, kept on screen.
void tooltip(context &ctx, const char *const *lines, i32 n, rgba edge) {
  f32 w = 0.0f;
  for (i32 i = 0; i < n; ++i)
    w = std::max(w, measure(ctx, lines[i]).x);
  const vec2 scr = screen_size(ctx);
  vec2 at = mouse_pos(ctx) + vec2{10.0f, 8.0f};
  const f32 line = font_size + 2.0f;
  const vec2 size{std::floor(w) + 10.0f, line * static_cast<f32>(n) + 6.0f};
  at.x = std::min(at.x, scr.x - size.x - 2.0f);
  at.y = std::min(at.y, scr.y - size.y - 2.0f);
  at = {std::floor(at.x), std::floor(at.y)};
  draw_rect(ctx, {at, size}, col_panel);
  draw_rect_lines(ctx, {at, size}, 1.0f, edge);
  for (i32 i = 0; i < n; ++i)
    text(ctx, lines[i], at + vec2{5.0f, 3.0f + line * static_cast<f32>(i)}, i == 0 ? col_gold_light : col_white);
}

void troop_tooltip(context &ctx) {
  if (ui_mouse_over(ctx) || state.cmd != command::none || state.menu.kind != menu_kind::none)
    return;
  const troop *c = nullptr;
  const i32 p = flag_at(ctx, side::player);
  if (p >= 0) {
    c = &state.board[static_cast<usize>(p)];
  } else {
    const i32 e = flag_at(ctx, side::enemy);
    if (e >= 0)
      c = &current_level().enemy[static_cast<usize>(e)];
  }
  if (!c) {
    vec2 mouse{};
    if (!mouse_on_table(ctx, &mouse))
      return;
    const terrain t = terrain_at(mouse);
    if (t == terrain::plain)
      return;
    static const char *what[] = {"", "Không đi qua được, phải đi vòng.", "Không đi qua được, phải đi vòng.",
                                  "Không lội được. Tìm bến cạn.", "Lội được, chậm.", "Chậm. Che nửa sát thương tên.",
                                  "Chỗ lội qua sông."};
    const char *lines[2] = {terrain_name(t), what[static_cast<i32>(t)]};
    tooltip(ctx, lines, 2, col_panel_edge);
    return;
  }
  char l1[96], l2[96];
  std::snprintf(l1, sizeof(l1), "%s %s · %s", c->owner == side::player ? "Ta:" : "Địch:", spec(c->type).name,
                tiers[c->tier].name);
  std::snprintf(l2, sizeof(l2), "%d lính", tiers[c->tier].men);
  const char *lines[2] = {l1, l2};
  tooltip(ctx, lines, 2, c->owner == side::player ? col_player : col_enemy_light);
}

// Why the order being given cannot go where the mouse is.
void command_hint(context &ctx) {
  if (ui_mouse_over(ctx) || state.selected < 0 || state.selected >= static_cast<i32>(state.board.size()))
    return;
  vec2 at{};
  const bool on_table = mouse_on_table(ctx, &at);
  const troop &c = state.board[static_cast<usize>(state.selected)];
  const char *err = nullptr;
  if (state.cmd == command::move)
    err = on_table ? troop_error(c.type, at, state.selected) : "Ngoài sa bàn";
  if (!err)
    return;
  const char *lines[1] = {err};
  tooltip(ctx, lines, 1, col_bad);
}

// --- Circle menu: arms to raise, or orders for a troop ---
//
// A ring cut into equal slices, one per item, the first at the top and the
// rest clockwise; each slice holds its item's symbol.

constexpr f32 radial_inner = 12.0f; // screen pixels
constexpr f32 radial_outer = 46.0f;

// The middle of the menu: the point it was opened at, kept on screen.
vec2 radial_center(context &ctx) {
  const f32 reach = radial_outer + 2.0f;
  const vec2 scr = screen_size(ctx);
  const vec2 c = table_to_screen(ctx, state.menu.at);
  return {std::floor(clamp(c.x, reach, scr.x - reach)), std::floor(clamp(c.y, reach + 48.0f, scr.y - reach))};
}

f32 radial_step() { return 2.0f * pi / static_cast<f32>(std::max<usize>(1, state.menu.items.size())); }

// Angle of the middle of slice `i`: the first one points up.
f32 radial_angle(usize i) { return -pi * 0.5f + radial_step() * static_cast<f32>(i); }

vec2 radial_item_pos(context &ctx, usize i) {
  const f32 a = radial_angle(i);
  const vec2 p = radial_center(ctx) + vec2{std::cos(a), std::sin(a)} * ((radial_inner + radial_outer) * 0.5f);
  return {std::floor(p.x), std::floor(p.y)};
}

// One slice of the ring, filled, as a fan of triangles.
void draw_slice(context &ctx, vec2 c, f32 a0, f32 a1, rgba col) {
  const i32 steps = std::max(2, static_cast<i32>((a1 - a0) / (pi / 24.0f)));
  for (i32 k = 0; k < steps; ++k) {
    const f32 u = a0 + (a1 - a0) * static_cast<f32>(k) / static_cast<f32>(steps);
    const f32 v = a0 + (a1 - a0) * static_cast<f32>(k + 1) / static_cast<f32>(steps);
    const vec2 du{std::cos(u), std::sin(u)}, dv{std::cos(v), std::sin(v)};
    draw_triangle(ctx, c + du * radial_inner, c + du * radial_outer, c + dv * radial_outer, col);
    draw_triangle(ctx, c + du * radial_inner, c + dv * radial_outer, c + dv * radial_inner, col);
  }
}

const char *order_name(order o) {
  static const char *names[order_count] = {"Đổi hướng", "Dời cờ", "Tăng quân", "Giảm quân", "Rút quân"};
  return names[static_cast<i32>(o)];
}

// A small line drawing for each order, about 12 pixels across.
void draw_order_icon(context &ctx, order o, vec2 c, rgba col) {
  switch (o) {
  case order::face:
    draw_arrow(ctx, c + vec2{-4.0f, 4.0f}, normalize(vec2{1.0f, -1.0f}), 11.0f, col);
    break;
  case order::move: // four ways
    for (const vec2 d : {vec2{1.0f, 0.0f}, vec2{-1.0f, 0.0f}, vec2{0.0f, 1.0f}, vec2{0.0f, -1.0f}})
      draw_arrow(ctx, c, d, 6.0f, col);
    break;
  case order::grow:
    draw_rect(ctx, {c + vec2{-5.0f, -1.0f}, {10.0f, 2.0f}}, col);
    draw_rect(ctx, {c + vec2{-1.0f, -5.0f}, {2.0f, 10.0f}}, col);
    break;
  case order::shrink:
    draw_rect(ctx, {c + vec2{-5.0f, -1.0f}, {10.0f, 2.0f}}, col);
    break;
  default: // withdraw: a cross
    draw_line(ctx, c + vec2{-5.0f, -5.0f}, c + vec2{5.0f, 5.0f}, 2.0f, col);
    draw_line(ctx, c + vec2{-5.0f, 5.0f}, c + vec2{5.0f, -5.0f}, 2.0f, col);
    break;
  }
}

void draw_radial(context &ctx) {
  const radial_menu &m = state.menu;
  if (m.kind == menu_kind::none || m.items.empty())
    return;
  const vec2 center = radial_center(ctx);
  const f32 step = radial_step();
  const i32 hover = radial_item_at(ctx);
  for (usize i = 0; i < m.items.size(); ++i) {
    const bool on = m.enabled[i];
    const bool hot = static_cast<i32>(i) == hover && on;
    const f32 mid = radial_angle(i);
    draw_slice(ctx, center, mid - step * 0.5f, mid + step * 0.5f, hot ? col_seal : on ? col_button : col_button_off);
    const vec2 p = radial_item_pos(ctx, i);
    const rgba ink = hot ? col_gold_light : on ? col_paper : rgb(110, 100, 86);
    if (m.kind == menu_kind::arms)
      draw_arm_symbol(ctx, p - vec2{5.0f, 5.0f}, static_cast<arm>(m.items[i]), 2.0f, ink);
    else
      draw_order_icon(ctx, static_cast<order>(m.items[i]), p, ink);
  }
  // The cuts between the slices, and the rims.
  if (m.items.size() > 1)
    for (usize i = 0; i < m.items.size(); ++i) {
      const f32 a = radial_angle(i) - step * 0.5f;
      const vec2 d{std::cos(a), std::sin(a)};
      draw_line(ctx, center + d * radial_inner, center + d * radial_outer, 1.0f, col_panel_edge);
    }
  draw_circle_lines(ctx, center, radial_outer, 1.0f, col_panel_edge);
  draw_circle_lines(ctx, center, radial_inner, 1.0f, col_panel_edge);
  // Where it points on the table.
  draw_circle(ctx, table_to_screen(ctx, m.at), 2.0f, col_gold_light);
  // The name of the item under the mouse, and for an arm what it is good at.
  if (hover < 0)
    return;
  if (m.kind == menu_kind::arms) {
    const arm a = static_cast<arm>(m.items[static_cast<usize>(hover)]);
    char title[96];
    std::snprintf(title, sizeof(title), "%s · %s %d lính", spec(a).name, tiers[state.new_tier].name,
                  tiers[state.new_tier].men);
    const auto lines = text_wrap(ctx, spec(a).desc, font_size, 240.0f, ui_font);
    std::vector<const char *> pointers{title};
    for (const auto &line : lines)
      pointers.push_back(line.c_str());
    tooltip(ctx, pointers.data(), static_cast<i32>(pointers.size()), col_panel_edge);
  } else {
    const order o = static_cast<order>(m.items[static_cast<usize>(hover)]);
    const char *lines[1] = {order_name(o)};
    tooltip(ctx, lines, 1, m.enabled[static_cast<usize>(hover)] ? col_panel_edge : col_bad);
  }
}

void result_popup(context &ctx) {
  const bool won = state.won;
  ui_style result = hud_style;
  result.panel.text = won ? col_paper : col_bad;
  result.panel.normal.outline = won ? col_panel_edge : col_seal;
  ui_style_set(ctx, result);

  ui_popup_begin(ctx, {.id = "result", .title = won ? "ĐẠI THẮNG" : "THẤT TRẬN", .width = 260.0f});
  hud_label(ctx, won ? "Quân địch tan vỡ." : "Quân ta vỡ trận.", col_white);
  char a[32], b[32], line[96];
  format_men(a, sizeof(a), state.men_start[0] - state.men_now[0]);
  format_men(b, sizeof(b), state.men_start[0]);
  std::snprintf(line, sizeof(line), "Ta mất: %s/%s lính", a, b);
  hud_label(ctx, line, rgb(240, 170, 160));
  format_men(a, sizeof(a), state.men_start[1] - state.men_now[1]);
  format_men(b, sizeof(b), state.men_start[1]);
  std::snprintf(line, sizeof(line), "Địch mất: %s/%s lính", a, b);
  hud_label(ctx, line, rgb(160, 200, 240));
  const i32 secs = static_cast<i32>(state.battle_time);
  std::snprintf(line, sizeof(line), "Giao chiến: %d:%02d", secs / 60, secs % 60);
  hud_label(ctx, line, col_white);
  if (ui_button(ctx, "BÀY LẠI TRẬN"))
    state.restart_requested = true;
  ui_popup_end(ctx);
  ui_style_set(ctx, hud_style);
}

} // namespace

i32 radial_item_at(context &ctx) {
  if (state.menu.kind == menu_kind::none)
    return -1;
  const vec2 d = mouse_pos(ctx) - radial_center(ctx);
  const f32 r = length(d);
  if (r < radial_inner || r > radial_outer + 4.0f || state.menu.items.empty())
    return -1;
  // Turned so the first slice runs from 0, then which slice the angle is in.
  f32 a = std::atan2(d.y, d.x) + pi * 0.5f + radial_step() * 0.5f;
  a = std::fmod(a + 4.0f * pi, 2.0f * pi);
  const usize i = static_cast<usize>(a / radial_step());
  return static_cast<i32>(std::min(i, state.menu.items.size() - 1));
}

i32 flag_at(context &ctx, side owner) {
  const std::vector<troop> &list = owner == side::player ? state.board : current_level().enemy;
  const vec2 mouse = mouse_pos(ctx);
  // The badge and the pole under it, a little wider to be easy to hit; the
  // nearest to the camera first.
  i32 best = -1;
  f32 best_y = -1e9f;
  for (i32 i = 0; i < static_cast<i32>(list.size()); ++i) {
    const vec2 at = list[static_cast<usize>(i)].pos;
    const vec2 top = badge_point(ctx, at);
    const vec2 foot = table_to_screen(ctx, at);
    const f32 x0 = std::min(top.x, foot.x) - 10.0f, x1 = std::max(top.x, foot.x) + 10.0f;
    const f32 y0 = top.y - 16.0f, y1 = foot.y + 4.0f;
    if (mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y0 && mouse.y <= y1 && foot.y > best_y) {
      best = i;
      best_y = foot.y;
    }
  }
  return best;
}

void render_init(context &ctx) {
  ui_font = font_load(ctx, "assets/fonts/BeVietnamPro-Bold.ttf", 32);
  tuft = make_tuft(ctx);
  hud_style = make_hud_style();
  post_fx_set(ctx, {.saturation = 1.08f, .vignette = 0.35f, .bloom = 0.55f, .bloom_threshold = 0.82f});
}

void render_cleanup(context &ctx) {
  if (ui_font.id != 0) {
    font_unload(ctx, ui_font);
    ui_font = {};
  }
  free_chunks(ctx);
  if (tuft.id != 0) {
    model_unload(ctx, tuft);
    tuft = {};
  }
  for (batch *b : {&frame, &tree_trunks, &tree_crowns, &bodies, &heads, &torches, &corpses, &marks,
                   &arrows, &shells, &puffs, &glows})
    if (b->buffer.id != 0) {
      instance_buffer_destroy(ctx, b->buffer);
      b->buffer = {};
    }
}

void render_world(context &ctx) {
  if (built_terrain != terrain_version()) {
    built_terrain = terrain_version();
    build_table(ctx);
  }
  set_light(ctx);
  const camera3d cam = table_camera();
  begin_3d(ctx, cam);
  add_lights(ctx);
  // The table under the sand table: dark wood, far out.
  material3d_set(ctx, {.specular = 0.1f, .cast_shadows = false});
  draw_plane3d(ctx, {static_cast<f32>(tiles_x) * 0.5f, table_bottom - 0.02f, static_cast<f32>(tiles_y) * 0.5f},
               {400.0f, 400.0f}, rgb(52, 36, 26));
  draw_ground(ctx, cam);
  draw_grass(ctx, cam);
  material3d_set(ctx, {.specular = 0.08f, .shininess = 12.0f});
  draw(ctx, mesh3d_cube, frame);
  draw(ctx, mesh3d_cylinder_low, tree_trunks);
  material3d_set(ctx, {.specular = 0.05f});
  draw(ctx, mesh3d_sphere_low, tree_crowns);
  material3d_set(ctx, {});
  draw_zones(ctx);
  if (state.screen == phase::deploy) {
    const bool pointing = state.cmd == command::none && state.menu.kind == menu_kind::none;
    const i32 hover = pointing ? flag_at(ctx, side::player) : -1;
    for (const troop &c : current_level().enemy)
      draw_flag3d(ctx, c.pos, c.tier, c.owner, false);
    for (usize i = 0; i < state.board.size(); ++i) {
      const troop &c = state.board[i];
      const i32 k = static_cast<i32>(i);
      const bool moving = k == state.selected && state.cmd == command::move;
      draw_flag3d(ctx, c.pos, c.tier, c.owner, k == hover || k == state.selected || k == state.menu.troop,
                  moving ? 0.4f : 1.0f);
    }
    // A troop being moved: its flag at the mouse, red where it cannot go.
    vec2 at{};
    const i32 sel = state.selected;
    if (state.cmd == command::move && sel >= 0 && sel < static_cast<i32>(state.board.size()) &&
        mouse_on_table(ctx, &at)) {
      const troop &c = state.board[static_cast<usize>(sel)];
      draw_flag3d(ctx, at, c.tier, c.owner, true, 0.9f, troop_error(c.type, at, sel) != nullptr);
    }
  } else {
    draw_battle(ctx);
  }
  // Water last, one sheet over the whole table: it shows wherever the ground
  // dips below it, over the beds and whatever wades in them.
  material3d_set(ctx, {.specular = 0.9f, .shininess = 80.0f, .cast_shadows = false});
  draw_plane3d(ctx, {static_cast<f32>(tiles_x) * 0.5f, water_level, static_cast<f32>(tiles_y) * 0.5f},
               {static_cast<f32>(tiles_x), static_cast<f32>(tiles_y)}, rgb(58, 110, 160, 190));
  material3d_set(ctx, {});
  end_3d(ctx);
}

void render_ui(context &ctx) {
  ui_style_set(ctx, hud_style);
  draw_overlays(ctx);
  top_bar(ctx);
  if (state.screen == phase::deploy) {
    // One line per command (types.h), in its order.
    static const char *hints[] = {
        "Phải: cắm cờ  /  Trái lên cờ: ra lệnh  /  Q E: xoay",
        "Trái: chọn hướng đánh  /  Phải, Esc: thôi",
        "Trái: chỗ cắm cờ mới  /  Phải, Esc: thôi",
    };
    text_shadow(ctx, hints[static_cast<i32>(state.cmd)], {110.0f, screen_size(ctx).y - 26.0f}, col_paper);
    deploy_bar(ctx);
    troop_tooltip(ctx);
    command_hint(ctx);
    draw_radial(ctx);
  } else {
    battle_bar(ctx);
    if (state.screen == phase::result)
      result_popup(ctx);
  }
}

} // namespace sandtable
