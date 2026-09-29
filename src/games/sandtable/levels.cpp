#include "levels.h"

#include <algorithm>
#include <cmath>

namespace sandtable {

namespace {

board_chip foe(arm a, i32 tier, vec2 pos) { return {a, tier, side::enemy, pos}; }

std::vector<level_def> make_levels() {
  std::vector<level_def> out;

  // 1. Small skirmish: spears standing in a line, a few archers behind.
  {
    level_def l{};
    l.name = "Tiền Đồn Đồi Tranh";
    l.brief = "Một toán thương binh giữ tiền đồn, cung thủ yểm trợ phía sau. Thương binh sợ tên.";
    l.budget = 130;
    l.max_tier = 1;
    l.max_chips = 6;
    l.enemy = {foe(arm::spear, 1, {820.0f, 250.0f}), foe(arm::spear, 1, {1180.0f, 250.0f}),
               foe(arm::archer, 1, {1000.0f, 130.0f})};
    l.seed = 11;
    l.clouds = 0.2f;
    l.hour = 9.0f;
    l.mountains = 0.0f;
    l.hill_amount = 0.06f;
    l.woods = 0.06f;
    l.streams = 1;
    out.push_back(l);
  }

  // 2. River with two fords, cavalry waiting on the far bank.
  {
    level_def l{};
    l.name = "Bến Đò Sông Lam";
    l.brief = "Kỵ binh địch chờ ở bờ bắc. Sông chỉ lội qua được ở hai bến cạn. Thương binh chặn kỵ.";
    l.budget = 560;
    l.max_tier = 2;
    l.max_chips = 7;
    l.enemy = {foe(arm::cavalry, 2, {700.0f, 220.0f}), foe(arm::archer, 2, {1000.0f, 130.0f}),
               foe(arm::infantry, 1, {1250.0f, 300.0f}), foe(arm::infantry, 1, {1450.0f, 300.0f}),
               foe(arm::boat, 1, {1700.0f, 300.0f})};
    l.river = true;
    l.river_y = 608.0f;
    l.fords = {{{420.0f, 540.0f}, {180.0f, 130.0f}}, {{1320.0f, 540.0f}, {220.0f, 130.0f}}};
    l.seed = 22;
    l.clouds = 0.6f;
    l.rain = 0.4f;
    l.hour = 15.0f;
    l.mountains = 0.03f;
    l.streams = 2;
    out.push_back(l);
  }

  // 3. Forests, first artillery.
  {
    level_def l{};
    l.name = "Rừng Lau Phục Kích";
    l.brief = "Pháo địch đặt giữa hai cánh rừng. Rừng làm chậm quân và che tên. Kỵ binh diệt pháo.";
    l.budget = 1600;
    l.max_tier = 3;
    l.max_chips = 8;
    l.enemy = {foe(arm::spear, 3, {1000.0f, 290.0f}), foe(arm::archer, 2, {700.0f, 200.0f}),
               foe(arm::archer, 2, {1300.0f, 200.0f}), foe(arm::artillery, 2, {1000.0f, 110.0f}),
               foe(arm::cavalry, 2, {1650.0f, 260.0f})};
    l.forests = {{{420.0f, 560.0f}, 170.0f}, {{1580.0f, 600.0f}, 190.0f}, {{1000.0f, 700.0f}, 90.0f}};
    l.seed = 33;
    l.clouds = 0.7f;
    l.hour = 17.5f; // an ambush at dusk
    l.woods = 0.12f;
    l.streams = 2;
    out.push_back(l);
  }

  // 4. Valley of guns.
  {
    level_def l{};
    l.name = "Thung Lũng Pháo";
    l.brief = "Pháo binh hai bên thung, bộ binh dày đặc ở giữa, voi chiến bên sườn. Đồi núi chắn lối, phải đi vòng.";
    l.budget = 3400;
    l.max_tier = 4;
    l.max_chips = 9;
    l.enemy = {foe(arm::artillery, 2, {420.0f, 150.0f}), foe(arm::infantry, 4, {1000.0f, 300.0f}),
               foe(arm::elephant, 2, {1650.0f, 300.0f}),
               foe(arm::spear, 3, {700.0f, 300.0f}), foe(arm::archer, 3, {1000.0f, 150.0f}),
               foe(arm::cavalry, 3, {1400.0f, 280.0f})};
    l.forests = {{{1000.0f, 600.0f}, 110.0f}};
    l.seed = 44;
    l.hour = 5.0f; // before dawn
    l.mountains = 0.07f;
    l.streams = 1;
    out.push_back(l);
  }

  // 5. The big one.
  {
    level_def l{};
    l.name = "Đại Chiến Trường Giang";
    l.brief = "Hai đại quân gặp nhau bên sông, núi chắn hai đầu. Mọi binh chủng, mọi cấp chip đều được dùng.";
    l.budget = 16000;
    l.max_tier = 6;
    l.max_chips = 10;
    l.enemy = {foe(arm::infantry, 5, {1000.0f, 300.0f}), foe(arm::spear, 4, {620.0f, 300.0f}),
               foe(arm::archer, 5, {1000.0f, 140.0f}), foe(arm::cavalry, 4, {1500.0f, 260.0f}),
               foe(arm::artillery, 4, {300.0f, 130.0f}), foe(arm::infantry, 4, {1330.0f, 330.0f}),
               foe(arm::infantry, 4, {300.0f, 320.0f}), foe(arm::elephant, 3, {700.0f, 200.0f}),
               foe(arm::boat, 3, {1200.0f, 330.0f})};
    l.river = true;
    l.river_y = 592.0f;
    l.fords = {{{200.0f, 520.0f}, {260.0f, 150.0f}}, {{870.0f, 520.0f}, {260.0f, 150.0f}},
               {{1540.0f, 520.0f}, {260.0f, 150.0f}}};
    l.forests = {{{1750.0f, 900.0f}, 140.0f}};
    l.seed = 55;
    l.clouds = 0.9f;
    l.rain = 0.85f;
    l.wind = {34.0f, 10.0f};
    l.hour = 21.0f; // a night battle in the storm
    l.mountains = 0.09f;
    l.streams = 3;
    out.push_back(l);
  }

  return out;
}

// --- The terrain of the current level -------------------------------------

std::vector<terrain> cells;
std::vector<tile_layers> tiles;
nav_grid nav;
nav_grid water_nav;

terrain get(i32 x, i32 y) {
  if (x < 0 || y < 0 || x >= tiles_x || y >= tiles_y)
    return terrain::plain;
  return cells[static_cast<usize>(y * tiles_x + x)];
}
void put(i32 x, i32 y, terrain t) {
  if (x >= 0 && y >= 0 && x < tiles_x && y < tiles_y)
    cells[static_cast<usize>(y * tiles_x + x)] = t;
}

vec2 center_of(i32 x, i32 y) {
  return {(static_cast<f32>(x) + 0.5f) * tile_world, (static_cast<f32>(y) + 0.5f) * tile_world};
}

bool high(terrain t) { return t == terrain::hill || t == terrain::mountain; }
bool rock(terrain t) { return t == terrain::mountain; }
bool wet(terrain t) { return t == terrain::river || t == terrain::ford; }

u32 hash(u32 a, u32 b, u32 c) {
  u32 h = 2166136261u;
  for (u32 v : {a, b, c})
    h = (h ^ v) * 16777619u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  return h ^ (h >> 15);
}

bool in_zone(i32 x, i32 y) {
  const vec2 c = center_of(x, y);
  return point_in_rect(c, player_zone) || point_in_rect(c, enemy_zone);
}

// Heights from noise make hills and mountains; a second noise makes woods.
void lay_noise(const level_def &l) {
  const noise_desc hn{.seed = l.seed, .frequency = 0.07f, .octaves = 4, .warp = 6.0f};
  const noise_desc wn{.seed = l.seed * 7 + 3, .frequency = 0.09f, .octaves = 3, .warp = 4.0f};
  std::vector<f32> h(cells.size()), w(cells.size());
  f32 wlo = 1.0f, whi = 0.0f;
  for (i32 y = 0; y < tiles_y; ++y)
    for (i32 x = 0; x < tiles_x; ++x) {
      const usize i = static_cast<usize>(y * tiles_x + x);
      h[i] = noise_2d(hn, static_cast<f32>(x), static_cast<f32>(y));
      w[i] = noise_2d(wn, static_cast<f32>(x), static_cast<f32>(y));
      wlo = std::min(wlo, w[i]);
      whi = std::max(whi, w[i]);
    }
  // The amounts are shares of the ground between the camps (no mountains
  // where the armies deploy), so take the thresholds from those cells.
  std::vector<f32> middle;
  for (i32 y = 0; y < tiles_y; ++y)
    for (i32 x = 0; x < tiles_x; ++x)
      if (!in_zone(x, y))
        middle.push_back(h[static_cast<usize>(y * tiles_x + x)]);
  std::sort(middle.begin(), middle.end());
  const auto quantile = [&](f32 share) {
    const f32 k = clamp(1.0f - share, 0.0f, 1.0f) * static_cast<f32>(middle.size() - 1);
    return share <= 0.0f ? 2.0f : middle[static_cast<usize>(k)];
  };
  const f32 mountain_at = quantile(l.mountains);
  const f32 hill_at = quantile(l.mountains + l.hill_amount);
  for (i32 y = 0; y < tiles_y; ++y)
    for (i32 x = 0; x < tiles_x; ++x) {
      const usize i = static_cast<usize>(y * tiles_x + x);
      const f32 wv = (w[i] - wlo) / std::max(0.001f, whi - wlo);
      // No high ground where the armies deploy.
      if (h[i] >= mountain_at && !in_zone(x, y))
        put(x, y, terrain::mountain);
      else if (h[i] >= hill_at && !in_zone(x, y))
        put(x, y, terrain::hill);
      else if (wv >= 1.0f - l.woods)
        put(x, y, terrain::forest);
    }
}

void stamp(const std::vector<terrain_blob> &blobs, terrain t) {
  for (const terrain_blob &b : blobs)
    for (i32 y = 0; y < tiles_y; ++y)
      for (i32 x = 0; x < tiles_x; ++x)
        if (length_sq(center_of(x, y) - b.pos) <= b.radius * b.radius &&
            (t != terrain::forest || !high(get(x, y))))
          put(x, y, t);
}

// A river two tiles wide, winding across the table, with its fords.
void carve_river(const level_def &l) {
  const noise_desc rn{.seed = l.seed + 101, .frequency = 0.06f, .octaves = 2};
  const i32 row = static_cast<i32>(l.river_y / tile_world);
  for (i32 x = 0; x < tiles_x; ++x) {
    const i32 y0 = row - 1 + static_cast<i32>(std::lround((noise_1d(rn, static_cast<f32>(x)) - 0.5f) * 4.0f));
    for (i32 y = y0; y <= y0 + 1; ++y) {
      bool at_ford = false;
      for (const rect &f : l.fords)
        at_ford = at_ford || (center_of(x, y).x >= f.pos.x && center_of(x, y).x <= f.pos.x + f.size.x);
      put(x, y, at_ford ? terrain::ford : terrain::river);
    }
  }
}

// Streams: from the top or bottom edge, a winding line of shallow water toward
// the middle of the table, going round high ground, until it meets water.
void carve_streams(const level_def &l) {
  rng r{static_cast<u64>(l.seed) * 31u + 7u};
  for (i32 s = 0; s < l.streams; ++s) {
    i32 x = r.range(4, tiles_x - 5);
    i32 y = r.range(0, 1) == 0 ? 0 : tiles_y - 1;
    const i32 dir = y == 0 ? 1 : -1;
    for (i32 step = 0; step < tiles_y * 3; ++step) {
      const terrain t = get(x, y);
      if (wet(t) || (t == terrain::stream && step > 0))
        break;
      if (!high(t))
        put(x, y, terrain::stream);
      i32 nx = x, ny = y + dir;
      if (r.chance(0.35f)) {
        nx = x + (r.chance(0.5f) ? 1 : -1);
        ny = y;
      }
      if (high(get(nx, ny))) {
        nx = x + (r.chance(0.5f) ? 1 : -1);
        ny = y;
      }
      if (nx < 0 || nx >= tiles_x || ny < 0 || ny >= tiles_y)
        break;
      x = nx;
      y = ny;
    }
  }
}

u8 nav_cost_of(terrain t) {
  switch (t) {
  case terrain::plain:
    return 2;
  case terrain::ford:
  case terrain::forest:
  case terrain::stream:
    return 3;
  default:
    return 0; // hills, mountains, the river
  }
}

// The ground round every enemy chip is open, so its men can march off it.
void clear_chips(const level_def &l) {
  for (const board_chip &e : l.enemy)
    for (i32 y = 0; y < tiles_y; ++y)
      for (i32 x = 0; x < tiles_x; ++x) {
        if (distance(e.pos, center_of(x, y)) > chip_radius(e.tier) + 2.0f * tile_world)
          continue;
        if (high(get(x, y)))
          put(x, y, terrain::plain);
        else if (get(x, y) == terrain::river)
          put(x, y, terrain::ford);
      }
}

// Boats: open river is best, fords and streams are shallow and slow.
u8 water_cost_of(terrain t) {
  switch (t) {
  case terrain::river:
    return 2;
  case terrain::ford:
  case terrain::stream:
    return 3;
  default:
    return 0;
  }
}

void build_nav() {
  nav = nav_grid_make({0.0f, 0.0f}, {tile_world, tile_world}, tiles_x, tiles_y);
  water_nav = nav_grid_make({0.0f, 0.0f}, {tile_world, tile_world}, tiles_x, tiles_y);
  for (i32 y = 0; y < tiles_y; ++y)
    for (i32 x = 0; x < tiles_x; ++x) {
      nav_set_cost(nav, {x, y}, nav_cost_of(get(x, y)));
      nav_set_cost(water_nav, {x, y}, water_cost_of(get(x, y)));
    }
}

// However the noise fell, the two camps must reach each other: if they do not,
// cut a way straight down the middle.
void ensure_connected() {
  std::vector<vec2> path;
  nav_path_opts opts{};
  opts.partial = false;
  if (nav_find_path(nav, rect_center(player_zone), rect_center(enemy_zone), path, opts))
    return;
  const i32 x = tiles_x / 2;
  for (i32 y = 0; y < tiles_y; ++y)
    for (i32 k = x; k <= x + 1; ++k) {
      const terrain t = get(k, y);
      if (high(t))
        put(k, y, terrain::plain);
      else if (t == terrain::river)
        put(k, y, terrain::ford);
    }
  build_nav();
}

// --- Tiles to draw ---

constexpr i16 row0(i32 col) { return static_cast<i16>(col); }
constexpr i16 blob(i32 row, i32 index) { return static_cast<i16>(row * tileset_columns + index); }
constexpr i32 row_hill = 1, row_mountain = 2, row_river = 3, row_stream = 4;

template <typename Same> u8 mask_of(i32 x, i32 y, Same same) {
  // Beyond the table counts as the same terrain: no shores along the frame.
  const auto s = [&](i32 ox, i32 oy) {
    const i32 nx = x + ox, ny = y + oy;
    return nx < 0 || ny < 0 || nx >= tiles_x || ny >= tiles_y || same(get(nx, ny));
  };
  u8 m = 0;
  m |= s(0, -1) ? neighbor_up : 0;
  m |= s(1, -1) ? neighbor_up_right : 0;
  m |= s(1, 0) ? neighbor_right : 0;
  m |= s(1, 1) ? neighbor_down_right : 0;
  m |= s(0, 1) ? neighbor_down : 0;
  m |= s(-1, 1) ? neighbor_down_left : 0;
  m |= s(-1, 0) ? neighbor_left : 0;
  m |= s(-1, -1) ? neighbor_up_left : 0;
  return m;
}

bool stream_like(terrain k) { return k == terrain::stream || wet(k); }

void build_tiles() {
  tiles.assign(cells.size(), {});
  for (i32 y = 0; y < tiles_y; ++y)
    for (i32 x = 0; x < tiles_x; ++x) {
      tile_layers &out = tiles[static_cast<usize>(y * tiles_x + x)];
      const terrain t = get(x, y);
      const u32 hv = hash(static_cast<u32>(x), static_cast<u32>(y), 99u);
      i32 n = 0;
      out.layer[n++] = row0(static_cast<i32>(hv % 4)); // grass under everything
      if (high(t))
        out.layer[n++] = blob(row_hill, autotile_index(mask_of(x, y, high)));
      if (rock(t))
        out.layer[n++] = blob(row_mountain, autotile_index(mask_of(x, y, rock)));
      if (wet(t))
        out.layer[n++] = blob(row_river, autotile_index(mask_of(x, y, wet)));
      if (t == terrain::stream)
        out.layer[n++] = blob(row_stream, autotile_index(mask_of(x, y, stream_like)));
      if (t == terrain::forest)
        out.layer[n++] = row0(4 + static_cast<i32>((hv >> 8) % 3));
      if (t == terrain::ford)
        out.layer[n++] = row0(7);
    }
}

} // namespace

const std::vector<level_def> &levels() {
  static const std::vector<level_def> all = make_levels();
  return all;
}

const level_def &current_level() { return levels()[static_cast<usize>(state.level)]; }

void build_terrain() {
  const level_def &l = current_level();
  cells.assign(static_cast<usize>(tiles_x * tiles_y), terrain::plain);
  lay_noise(l);
  stamp(l.hills, terrain::hill);
  stamp(l.forests, terrain::forest);
  // A lone mountain tile or a speck of hill is noise, not a feature.
  {
    tile_grid g = tile_grid_make(tiles_x, tiles_y);
    for (usize i = 0; i < cells.size(); ++i)
      g.cells[i] = static_cast<i32>(cells[i]);
    grid_remove_small(g, static_cast<i32>(terrain::mountain), 4, static_cast<i32>(terrain::hill));
    grid_remove_small(g, static_cast<i32>(terrain::hill), 6, static_cast<i32>(terrain::plain));
    grid_remove_small(g, static_cast<i32>(terrain::forest), 3, static_cast<i32>(terrain::plain));
    for (usize i = 0; i < cells.size(); ++i)
      cells[i] = static_cast<terrain>(g.cells[i]);
  }
  if (l.river)
    carve_river(l);
  carve_streams(l);
  clear_chips(l);
  build_nav();
  ensure_connected();
  build_tiles();
}

terrain terrain_cell(i32 x, i32 y) { return get(x, y); }

terrain terrain_at(vec2 pos) {
  return get(static_cast<i32>(std::floor(pos.x / tile_world)), static_cast<i32>(std::floor(pos.y / tile_world)));
}

bool walkable(vec2 pos, bool boat) {
  if (pos.x < 0.0f || pos.y < 0.0f || pos.x >= world_width || pos.y >= world_height)
    return false;
  const terrain t = terrain_at(pos);
  return (boat ? water_cost_of(t) : nav_cost_of(t)) != 0;
}

const nav_grid &terrain_nav(bool boat) { return boat ? water_nav : nav; }

vec2 nearest_water(vec2 pos, f32 *dist) {
  vec2 best = pos;
  f32 best_d = 1e9f;
  for (i32 y = 0; y < tiles_y; ++y)
    for (i32 x = 0; x < tiles_x; ++x) {
      if (water_cost_of(get(x, y)) == 0)
        continue;
      const f32 d = distance(center_of(x, y), pos);
      if (d < best_d) {
        best_d = d;
        best = center_of(x, y);
      }
    }
  if (dist)
    *dist = best_d;
  return best;
}

const std::vector<tile_layers> &terrain_tiles() { return tiles; }

f32 terrain_speed(terrain t, bool boat) {
  if (boat)
    return t == terrain::river ? 1.0f : 0.6f;
  switch (t) {
  case terrain::forest:
    return 0.65f;
  case terrain::stream:
    return 0.7f;
  case terrain::ford:
    return 0.75f;
  default:
    return 1.0f;
  }
}

bool terrain_covers(terrain t) { return t == terrain::forest; }

const char *terrain_name(terrain t) {
  switch (t) {
  case terrain::hill:
    return "Đồi";
  case terrain::mountain:
    return "Núi";
  case terrain::river:
    return "Sông";
  case terrain::stream:
    return "Suối";
  case terrain::forest:
    return "Rừng";
  case terrain::ford:
    return "Bến cạn";
  default:
    return "Đồng bằng";
  }
}

} // namespace sandtable
