#include "geometry.h"

// The city map's own helpers: shapes and lookups by position.

#include <cmath>

namespace sandtable::city {

// --- Small shapes -------------------------------------------------------------

bool obb::contains(vec2 p) const {
  const vec2 d = p - center;
  return std::fabs(dot(d, axis_x())) <= half.x && std::fabs(dot(d, axis_y())) <= half.y;
}

vec2 obb::corner(i32 i) const {
  const f32 sx = (i == 0 || i == 3) ? -1.0f : 1.0f;
  const f32 sy = i < 2 ? -1.0f : 1.0f;
  return center + axis_x() * (half.x * sx) + axis_y() * (half.y * sy);
}

f32 lake_shape::radius_at(f32 degrees) const {
  if (r.empty())
    return radius;
  const i32 n = static_cast<i32>(r.size());
  f32 k = degrees / 360.0f * static_cast<f32>(n);
  k = std::fmod(k, static_cast<f32>(n));
  if (k < 0.0f)
    k += static_cast<f32>(n);
  const i32 i = static_cast<i32>(k) % n;
  const f32 t = k - std::floor(k);
  return lerp(r[static_cast<size_t>(i)], r[static_cast<size_t>((i + 1) % n)], t);
}

const cell_info *city_map::cell_at(vec2 p) const {
  const i32 x = static_cast<i32>(std::floor(p.x / desc.cell)), y = static_cast<i32>(std::floor(p.y / desc.cell));
  return inside(x, y) ? &at(x, y) : nullptr;
}

cell_info *city_map::cell_at(vec2 p) {
  const i32 x = static_cast<i32>(std::floor(p.x / desc.cell)), y = static_cast<i32>(std::floor(p.y / desc.cell));
  return inside(x, y) ? &at(x, y) : nullptr;
}

i32 city_map::block_at(vec2 p) const {
  const cell_info *c = cell_at(p);
  return c ? c->block : -1;
}

i32 city_map::district_at(vec2 p) const {
  const cell_info *c = cell_at(p);
  return c ? c->district : -1;
}

i32 city_map::building_at(vec2 p) const {
  const cell_info *c = cell_at(p);
  if (!c || c->building < 0)
    return -1;
  // The cell is the building's when its centre is inside; check the point.
  return buildings[static_cast<size_t>(c->building)].box.contains(p) ? c->building : -1;
}

i32 city_map::road_at(vec2 p) const {
  const cell_info *c = cell_at(p);
  return c && (c->g == ground::road || c->g == ground::bridge) ? c->road_id : -1;
}

bool city_map::walkable(vec2 p) const {
  const cell_info *c = cell_at(p);
  return c && c->g != ground::building && c->g != ground::water;
}

bool city_map::on_road(vec2 p, f32 slack) const {
  const i32 x = static_cast<i32>(std::floor(p.x / desc.cell)), y = static_cast<i32>(std::floor(p.y / desc.cell));
  for (i32 yy = y - 1; yy <= y + 1; ++yy)
    for (i32 xx = x - 1; xx <= x + 1; ++xx) {
      if (!inside(xx, yy))
        continue;
      const cell_info &c = at(xx, yy);
      if ((c.g != ground::road && c.g != ground::bridge) || c.road_id < 0)
        continue;
      const road &rd = roads[static_cast<size_t>(c.road_id)];
      vec2 q;
      if (closest_on(rd.pts, p, q) < rd.reach() - slack)
        return true;
    }
  return false;
}

} // namespace sandtable::city
