#include "levels.h"

#include <algorithm>
#include <cmath>

namespace sandtable {

namespace {

troop foe(i32 tier, vec2 pos) { return {tier, side::enemy, pos}; }

// Two gangs either side of a river. The Rồng Xanh hold their gambling den in
// the north and have their eyes on everything between.
level_def make_level() {
  level_def l{};
  l.name = "Tranh Giành Địa Bàn";
  l.brief = "Băng Rồng Xanh giữ Sòng Bạc và nhòm ngó cả vùng. Giữ nhiều địa bàn hơn khi hết giờ, "
            "hoặc hạ hết chúng.";
  l.enemy = {foe(3, {1024.0f, 250.0f}), foe(2, {620.0f, 280.0f}), foe(2, {1430.0f, 280.0f}),
             foe(1, {330.0f, 320.0f}), foe(1, {1720.0f, 320.0f}), foe(0, {1024.0f, 110.0f})};
  // The player starts with one turf, the pawn shop in the south, and his
  // men there.
  l.turfs = {{"Tiệm Cầm Đồ", {1024.0f, 1030.0f}, 150.0f, static_cast<i32>(side::player)},
             {"Chợ Đầu Mối", {330.0f, 730.0f}},        {"Bến Xe", {1024.0f, 750.0f}},
             {"Quán Nhậu", {1720.0f, 730.0f}},         {"Bến Cảng", {330.0f, 440.0f}},
             {"Phố Đèn Lồng", {1024.0f, 440.0f}},      {"Bãi Xe Tải", {1720.0f, 440.0f}},
             {"Sòng Bạc", {1024.0f, 200.0f}, 150.0f, static_cast<i32>(side::enemy)}};
  l.home_turf = 0;
  l.river = true;
  l.river_y = 592.0f;
  l.fords = {{{200.0f, 520.0f}, {260.0f, 150.0f}}, {{870.0f, 520.0f}, {260.0f, 150.0f}},
             {{1540.0f, 520.0f}, {260.0f, 150.0f}}};
  l.forests = {{{1750.0f, 900.0f}, 140.0f}};
  l.seed = 55;
  l.hour = 21.0f; // at night
  l.mountains = 0.09f;
  l.streams = 3;
  return l;
}

// --- The terrain of the current level -------------------------------------

std::vector<terrain> cells;
nav_grid nav;

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
bool wet(terrain t) { return t == terrain::river || t == terrain::ford; }

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

// The ground round every enemy camp and the middle of every turf is open,
// so men can walk off the one and stand on the other.
void clear_camps(const level_def &l) {
  const auto open = [](vec2 at, f32 radius) {
    for (i32 y = 0; y < tiles_y; ++y)
      for (i32 x = 0; x < tiles_x; ++x) {
        if (distance(at, center_of(x, y)) > radius)
          continue;
        if (high(get(x, y)))
          put(x, y, terrain::plain);
        else if (get(x, y) == terrain::river)
          put(x, y, terrain::ford);
      }
  };
  for (const troop &e : l.enemy)
    open(e.pos, 2.5f * tile_world);
  for (const turf_def &t : l.turfs)
    open(t.pos, 2.0f * tile_world);
}

void build_nav() {
  nav = nav_grid_make({0.0f, 0.0f}, {tile_world, tile_world}, tiles_x, tiles_y);
  for (i32 y = 0; y < tiles_y; ++y)
    for (i32 x = 0; x < tiles_x; ++x) {
      nav_set_cost(nav, {x, y}, nav_cost_of(get(x, y)));
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

} // namespace

const level_def &current_level() {
  static const level_def level = make_level();
  return level;
}

namespace {
u32 built_count = 0;
} // namespace

u32 terrain_version() { return built_count; }

void build_terrain() {
  ++built_count;
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
  clear_camps(l);
  build_nav();
  ensure_connected();
}

terrain terrain_cell(i32 x, i32 y) { return get(x, y); }

terrain terrain_at(vec2 pos) {
  return get(static_cast<i32>(std::floor(pos.x / tile_world)), static_cast<i32>(std::floor(pos.y / tile_world)));
}

bool walkable(vec2 pos) {
  if (pos.x < 0.0f || pos.y < 0.0f || pos.x >= world_width || pos.y >= world_height)
    return false;
  return nav_cost_of(terrain_at(pos)) != 0;
}

const nav_grid &terrain_nav() { return nav; }

f32 terrain_speed(terrain t) {
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
