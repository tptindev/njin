#include "render_common.h"

#include <algorithm>
#include <cmath>

// The flat city: ground from the raster, water with smooth banks, roads with
// their sidewalks and markings, and the open places. One set of meshes for
// the normal look and one per debug overlay, made when first shown.

namespace sandtable::city {

namespace {

mesh_set raster;                                    // the raster ground
mesh_set surfaces;                                  // water, roads, places
mesh_set overlays[static_cast<i32>(overlay::count)]; // ground of each overlay
const city_map *built_for = nullptr;

constexpr rgba col_water = rgb8(52, 92, 104);
constexpr rgba col_lake = rgb8(58, 104, 110);
constexpr rgba col_rim = rgb8(150, 146, 136);
constexpr rgba col_sidewalk = rgb8(176, 168, 152);
constexpr rgba col_asphalt = rgb8(60, 60, 64);
constexpr rgba col_alley = rgb8(118, 114, 108);
constexpr rgba col_marking = rgb8(226, 222, 206);
constexpr rgba col_grass = rgb8(96, 142, 72);

rgba overlay_color(const city_map &map, i32 x, i32 y, overlay o) {
  const cell_info &c = map.at(x, y);
  switch (o) {
  case overlay::districts: {
    if (c.g == ground::water)
      return rgb8(30, 40, 60);
    const district &d = map.districts[c.district];
    rgba k = shade(district_color(d.kind), 0.75f + 0.25f * pick01(c.district, 11));
    return c.g == ground::road || c.g == ground::bridge ? shade(k, 0.55f) : k;
  }
  case overlay::blocks:
    if (c.block < 0)
      return c.g == ground::water ? rgb8(30, 40, 60) : rgb8(70, 70, 74);
    return rgba{0.3f + 0.6f * pick01(static_cast<u32>(c.block), 1), 0.3f + 0.6f * pick01(static_cast<u32>(c.block), 2),
                0.3f + 0.6f * pick01(static_cast<u32>(c.block), 3), 1.0f};
  case overlay::foot: {
    const u8 cost = nav_cost(map.foot, {x, y});
    return cost == 0 ? rgb8(90, 30, 30) : cost == 1 ? rgb8(220, 220, 200) : rgb8(150, 170, 120);
  }
  case overlay::car:
    return nav_cost(map.car, {x, y}) ? rgb8(80, 140, 230) : rgb8(50, 50, 56);
  default:
    break;
  }
  // Under a road the vector sidewalk covers the cell only partly: the part
  // that shows takes the colour of the open ground beside it, so there is no
  // raster staircase along a promenade or a park.
  if (c.g == ground::road) {
    const i32 nx[4] = {x + 1, x - 1, x, x}, ny[4] = {y, y, y + 1, y - 1};
    for (i32 k = 0; k < 4; ++k)
      if (map.inside(nx[k], ny[k])) {
        const cell_info &n = map.at(nx[k], ny[k]);
        if (n.g == ground::plaza || n.g == ground::park || n.g == ground::lot)
          return ground_color(map, n);
      }
  }
  return ground_color(map, c);
}

bool same(rgba a, rgba b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

// The raster, a quad for each run of cells of one colour along a row.
void build_raster(context &ctx, const city_map &map, overlay o, mesh_set &out) {
  mesh_builder b;
  const f32 cs = map.desc.cell;
  for (i32 y = 0; y < map.rows; ++y) {
    i32 x = 0;
    while (x < map.cols) {
      const rgba c = overlay_color(map, x, y, o);
      // A run stops at a tile's edge, to stay inside its tile's model.
      const i32 tile_cells = std::max(1, static_cast<i32>(mesh_tile / cs));
      i32 end = x + 1;
      while (end < map.cols && end % tile_cells != 0 && same(overlay_color(map, end, y, o), c))
        ++end;
      const f32 h = map.at(x, y).g == ground::water ? layer_water : layer_ground;
      const f32 x0 = static_cast<f32>(x) * cs, x1 = static_cast<f32>(end) * cs;
      const f32 y0 = static_cast<f32>(y) * cs, y1 = static_cast<f32>(y + 1) * cs;
      b.quad({x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, h, c);
      x = end;
    }
  }
  out.build(ctx, b);
}

void build_water(const city_map &map, mesh_builder &b) {
  if (map.river.on) {
    b.band(map.river.pts, map.river.width + 6.0f, layer_rim, col_rim);
    b.band(map.river.pts, map.river.width, layer_surface, col_water);
  }
  if (map.lake.on) {
    const lake_shape &lk = map.lake;
    constexpr i32 n = 96;
    for (i32 i = 0; i < n; ++i) {
      const f32 a0 = static_cast<f32>(i) / n * 360.0f, a1 = static_cast<f32>(i + 1) / n * 360.0f;
      const vec2 p0 = from_angle(a0), p1 = from_angle(a1);
      b.tri(lk.center, lk.center + p0 * (lk.radius_at(a0) + 3.0f), lk.center + p1 * (lk.radius_at(a1) + 3.0f),
            layer_rim, col_rim);
      b.tri(lk.center, lk.center + p0 * lk.radius_at(a0), lk.center + p1 * lk.radius_at(a1), layer_surface, col_lake);
    }
  }
}

void build_roads(const city_map &map, mesh_builder &b) {
  for (i32 id = 0; id < static_cast<i32>(map.roads.size()); ++id) {
    const road &rd = map.roads[static_cast<size_t>(id)];
    if (rd.kind == road_kind::alley) {
      b.band(rd.pts, rd.width, layer_alley, col_alley);
      continue;
    }
    b.band(rd.pts, rd.width + rd.sidewalk * 2.0f, layer_sidewalk, col_sidewalk);
    b.band(rd.pts, rd.width, layer_road, col_asphalt);
    if (rd.kind != road_kind::avenue)
      continue;
    // A dashed centre line, broken where another road has the junction.
    for (size_t i = 0; i + 1 < rd.pts.size(); ++i) {
      const vec2 a = rd.pts[i], d = rd.pts[i + 1] - a;
      const f32 len = length(d);
      for (f32 s = 0.0f; s + 10.0f < len; s += 22.0f) {
        const vec2 p = a + d * (s / len), q = a + d * ((s + 10.0f) / len);
        const cell_info *c = map.cell_at((p + q) * 0.5f);
        if (c && c->road_id == id)
          b.line(p, q, 1.6f, layer_marking, col_marking);
      }
    }
  }
}

void build_places(const city_map &map, mesh_builder &b) {
  for (const spot &s : map.spots) {
    const obb &o = s.box;
    const vec2 u = o.axis_x(), v = o.axis_y();
    switch (s.kind) {
    case spot_kind::parking:
      b.rect(o, layer_surface, rgb8(84, 84, 88));
      for (f32 a = -o.half.x + 6.0f; a < o.half.x - 4.0f; a += 8.0f)
        b.line(o.center + u * a + v * (o.half.y - 10.0f), o.center + u * a + v * o.half.y * 0.2f, 0.8f,
               layer_surface + 0.004f, col_marking);
      break;
    case spot_kind::sports_field: {
      const obb field{o.center, o.half - vec2{4.0f, 4.0f}, o.angle};
      b.rect(field, layer_surface, rgb8(84, 150, 70));
      for (i32 k = 0; k < 4; ++k)
        b.line(field.corner(k), field.corner((k + 1) % 4), 1.0f, layer_surface + 0.004f, col_marking);
      b.line(o.center - v * field.half.y, o.center + v * field.half.y, 1.0f, layer_surface + 0.004f, col_marking);
      break;
    }
    case spot_kind::market_square:
      b.rect(o, layer_surface, rgb8(200, 170, 130));
      break;
    case spot_kind::container_yard:
      b.rect(o, layer_surface, rgb8(128, 124, 116));
      break;
    case spot_kind::roundabout:
      b.disc(o.center, o.half.x + 2.0f, layer_island, rgb8(190, 186, 176), 32);
      b.disc(o.center, o.half.x - 2.0f, layer_island + 0.004f, col_grass, 32);
      break;
    default:
      break;
    }
  }
}

} // namespace

void ground_build(context &ctx, const city_map &map) {
  built_for = &map;
  build_raster(ctx, map, overlay::none, raster);
  mesh_builder b;
  build_water(map, b);
  build_places(map, b);
  build_roads(map, b);
  surfaces.build(ctx, b);
}

void ground_draw(context &ctx, const city_map &map, const view_options &opt) {
  material3d_set(ctx, {.specular = 0.04f, .shininess = 8.0f, .cast_shadows = false});
  if (opt.layer == overlay::none) {
    raster.draw(ctx);
  } else {
    mesh_set &o = overlays[static_cast<i32>(opt.layer)];
    if (o.models.empty() && built_for == &map)
      build_raster(ctx, map, opt.layer, o);
    o.draw(ctx);
  }
  // The nav overlays are about the raster: nothing drawn over it.
  if (opt.layer != overlay::foot && opt.layer != overlay::car)
    surfaces.draw(ctx);
  material3d_set(ctx, {});
}

void ground_cleanup(context &ctx) {
  raster.destroy(ctx);
  surfaces.destroy(ctx);
  for (mesh_set &o : overlays)
    o.destroy(ctx);
  built_for = nullptr;
}

} // namespace sandtable::city
