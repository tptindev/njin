#include "gen.h"
#include "street_kit.h"

#include <algorithm>
#include <cmath>

// Stages 14 and 15: the businesses and the street furniture.

namespace sandtable::city {

business_kind generator::pick(const std::vector<weight> &table, rng &r) {
  f32 total = 0.0f;
  for (const weight &w : table)
    total += w.w;
  f32 x = r.range(0.0f, total);
  for (const weight &w : table) {
    if (x < w.w)
      return w.k;
    x -= w.w;
  }
  return table.back().k;
}

std::vector<generator::weight> generator::table_for(district_kind k) {
  using b = business_kind;
  switch (k) {
  case district_kind::old_quarter:
    return {{b::cafe, 3}, {b::street_food, 4}, {b::restaurant, 2}, {b::grocery, 2}, {b::gold_shop, 1.5f},
            {b::pawn_shop, 1}, {b::pharmacy, 1}, {b::guest_house, 1}, {b::bike_repair, 1}};
  case district_kind::market:
    return {{b::street_food, 4}, {b::grocery, 3}, {b::cafe, 2}, {b::gold_shop, 1.5f}, {b::pawn_shop, 1.5f},
            {b::pharmacy, 1}, {b::bike_repair, 1}};
  case district_kind::nightlife:
    return {{b::bar, 3}, {b::karaoke, 3}, {b::massage, 2}, {b::guest_house, 2}, {b::street_food, 2},
            {b::billiards, 1.5f}, {b::cafe, 1}, {b::pawn_shop, 1}};
  case district_kind::docks:
    return {{b::street_food, 3}, {b::cafe, 2}, {b::bike_repair, 2}, {b::guest_house, 1}, {b::billiards, 1}};
  case district_kind::industrial:
    return {{b::street_food, 3}, {b::cafe, 2}, {b::bike_repair, 3}, {b::grocery, 1}};
  case district_kind::new_urban:
    return {{b::restaurant, 3}, {b::cafe, 3}, {b::pharmacy, 1.5f}, {b::gold_shop, 1}, {b::karaoke, 1},
            {b::grocery, 1}};
  default:
    return {{b::grocery, 3}, {b::cafe, 3}, {b::street_food, 3}, {b::bike_repair, 2}, {b::pharmacy, 1},
            {b::billiards, 1}, {b::pawn_shop, 0.6f}};
  }
}

i32 generator::base_income(business_kind k) {
  static const i32 v[] = {800, 1200, 3000, 700, 1500, 6000, 2500, 5000, 4000, 1500,
                          3000, 2000, 8000, 600, 5000, 20000, 7000, 3000, 10000};
  return v[static_cast<i32>(k)];
}

void generator::add_business(i32 bi, business_kind kind, rng &r) {
  building &bd = m.buildings[static_cast<size_t>(bi)];
  business bz;
  bz.kind = kind;
  bz.building = bi;
  bz.block = bd.block;
  bz.district = bd.district;
  bz.door = bd.door;
  const f32 size = bd.box.half.x * bd.box.half.y * 4.0f;
  bz.tier = size > 9000.0f || kind == business_kind::market || kind == business_kind::hotel ? 3
            : (bd.road >= 0 && m.roads[static_cast<size_t>(bd.road)].kind == road_kind::avenue) || size > 4000.0f ? 2
                                                                                                                : 1;
  bz.income = static_cast<i32>(static_cast<f32>(base_income(kind)) * (0.6f + 0.4f * static_cast<f32>(bz.tier)) *
                               r.range(0.75f, 1.3f));
  bz.protection = static_cast<i32>(static_cast<f32>(bz.income) * 7.0f * r.range(0.08f, 0.15f));
  const char *owner = owner_names[r.range(0, owner_name_count - 1)];
  std::string prefix = business_name(kind);
  if (kind == business_kind::street_food)
    prefix = food_names[r.range(0, food_name_count - 1)];
  if (kind == business_kind::market && bd.district >= 0)
    bz.name = "Chợ " + m.districts[static_cast<size_t>(bd.district)].name;
  else if (kind == business_kind::gambling_den)
    bz.name = std::string("Nhà ") + owner;
  else
    bz.name = prefix + " " + owner;
  if (bd.road >= 0) {
    const std::string &rn = m.roads[static_cast<size_t>(bd.road)].name;
    bz.address = bd.number > 0 ? std::to_string(bd.number) + " " + rn : rn;
  }
  bd.business = static_cast<i32>(m.businesses.size());
  m.businesses.push_back(std::move(bz));
}

void generator::make_businesses() {
  rng r = stage_rng(14);
  for (i32 bi = 0; bi < static_cast<i32>(m.buildings.size()); ++bi) {
    const building &bd = m.buildings[static_cast<size_t>(bi)];
    if (!bd.door_ok || bd.district < 0)
      continue;
    const district_kind dk = m.districts[static_cast<size_t>(bd.district)].kind;
    const road_kind rk = bd.road >= 0 ? m.roads[static_cast<size_t>(bd.road)].kind : road_kind::alley;
    if (bd.business == -2) {
      m.buildings[static_cast<size_t>(bi)].business = -1;
      add_business(bi, business_kind::gas_station, r);
      continue;
    }
    switch (bd.kind) {
    case building_kind::market_hall:
      add_business(bi, business_kind::market, r);
      break;
    case building_kind::warehouse:
      if (r.chance(0.7f))
        add_business(bi, business_kind::warehouse, r);
      break;
    case building_kind::workshop:
      if (r.chance(0.55f))
        add_business(bi, business_kind::workshop, r);
      break;
    case building_kind::hotel:
      add_business(bi, business_kind::hotel, r);
      break;
    case building_kind::apartment:
      if (r.chance(0.4f))
        add_business(bi, r.chance(0.5f) ? business_kind::restaurant : business_kind::cafe, r);
      break;
    case building_kind::tube_house:
      if (r.chance(prof(dk).business))
        add_business(bi, pick(table_for(dk), r), r);
      break;
    case building_kind::house:
      if (bd.road >= 0 && rk == road_kind::alley && r.chance(0.07f)) {
        static const std::vector<weight> alley_trade = {{business_kind::cafe, 3},
                                                        {business_kind::grocery, 3},
                                                        {business_kind::street_food, 2},
                                                        {business_kind::gambling_den, 1.2f},
                                                        {business_kind::billiards, 1}};
        add_business(bi, pick(alley_trade, r), r);
      }
      break;
    default:
      break;
    }
  }
}

bool generator::on_sidewalk(vec2 p) const {
  const cell_info *c = m.cell_at(p);
  return c && c->g == ground::road && !c->carriage;
}

void generator::make_props() {
  rng r = stage_rng(15);
  // Trees and lamps along the avenues and big streets, poles along the rest.
  for (const road &rd : m.roads) {
    const polyline_walk walk(rd.pts);
    if (rd.kind == road_kind::alley) {
      for (f32 s = 30.0f; s < walk.length(); s += r.range(70.0f, 110.0f)) {
        vec2 p, t;
        walk.at(s, p, t);
        const vec2 q = p + perp(t) * (rd.width * 0.5f - 2.0f) * (r.chance(0.5f) ? 1.0f : -1.0f);
        const cell_info *c = m.cell_at(q);
        if (c && c->g == ground::road && c->road == road_kind::alley)
          m.props.push_back({prop_kind::pole, q, angle_of(t), 0.8f, r.next_u32()});
      }
      continue;
    }
    const bool leafy = rd.kind == road_kind::avenue || rd.kind == road_kind::ring || rd.sidewalk >= 10.0f;
    // In a strip along the curb (sidewalk_layout.json's lamps and poles), so
    // the rest of the sidewalk stays a walkway.
    const f32 off = rd.width * 0.5f + std::min(2.4f, rd.sidewalk * 0.3f);
    for (const f32 side : {-1.0f, 1.0f}) {
      const f32 gap = leafy ? r.range(40.0f, 52.0f) : r.range(64.0f, 80.0f);
      for (f32 s = r.range(0.0f, gap); s < walk.length(); s += gap + r.range(-6.0f, 6.0f)) {
        vec2 p, t;
        walk.at(s, p, t);
        const vec2 q = p + perp(t) * (off * side);
        if (!on_sidewalk(q))
          continue;
        if (leafy)
          // A lamp's arm (the kit's +X) reaches out over the road.
          m.props.push_back({r.chance(0.8f) ? prop_kind::tree : prop_kind::lamp, q, angle_of(perp(t) * -side),
                             r.range(0.8f, 1.2f), r.next_u32()});
        else if (side > 0.0f)
          m.props.push_back({prop_kind::pole, q, angle_of(t), 1.0f, r.next_u32()});
        else if (r.chance(0.3f))
          m.props.push_back({prop_kind::tree, q, r.range(0.0f, 360.0f), r.range(0.7f, 1.0f), r.next_u32()});
      }
    }
  }
  // Motorbikes parked nose-in before shops, stools out before eateries.
  for (const business &bz : m.businesses) {
    const building &bd = m.buildings[static_cast<size_t>(bz.building)];
    if (bd.road < 0)
      continue;
    const vec2 fwd = bd.front(), side = bd.box.axis_x();
    const vec2 face = bd.box.center + fwd * bd.box.half.y;
    const i32 bikes = r.range(1, 4) + (bz.tier > 1 ? 2 : 0);
    // Where the door is along the front: no bike parked across it.
    const f32 door_along = dot(bd.door - bd.box.center, side);
    for (i32 i = 0; i < bikes; ++i) {
      const f32 along = r.range(-bd.box.half.x + 3.0f, bd.box.half.x - 3.0f);
      const vec2 q = face + fwd * r.range(5.0f, 8.0f) + side * along;
      if (std::fabs(along - door_along) < 6.0f)
        continue;
      if (on_sidewalk(q) || (m.cell_at(q) && m.cell_at(q)->g == ground::lot))
        m.props.push_back({prop_kind::motorbike, q, angle_of(fwd) + r.range(-12.0f, 12.0f), 1.0f, r.next_u32()});
    }
    if (bz.kind == business_kind::street_food || bz.kind == business_kind::cafe) {
      for (i32 i = r.range(3, 8); i > 0; --i) {
        const f32 along = r.range(-bd.box.half.x, bd.box.half.x);
        // Against the shop's front: the walkway beyond stays clear.
        const vec2 q = face + fwd * r.range(2.0f, 4.5f) + side * along;
        if (std::fabs(along - door_along) < 5.0f)
          continue;
        if (on_sidewalk(q))
          m.props.push_back({prop_kind::stool, q, r.range(0.0f, 90.0f), 1.0f, r.next_u32()});
      }
    }
  }
  // What fills the open places.
  for (const spot &sp : m.spots) {
    const vec2 u = sp.box.axis_x(), v = sp.box.axis_y();
    switch (sp.kind) {
    case spot_kind::market_square:
      for (f32 a = -sp.box.half.x + 10.0f; a < sp.box.half.x - 8.0f; a += 16.0f)
        for (f32 b = -sp.box.half.y + 10.0f; b < sp.box.half.y - 8.0f; b += 18.0f)
          if (r.chance(0.8f))
            m.props.push_back({prop_kind::stall, sp.box.center + u * a + v * b, sp.box.angle, 1.0f, r.next_u32()});
      break;
    case spot_kind::container_yard:
      for (f32 a = -sp.box.half.x + 18.0f; a < sp.box.half.x - 16.0f; a += 34.0f)
        for (f32 b = -sp.box.half.y + 10.0f; b < sp.box.half.y - 8.0f; b += 14.0f)
          if (r.chance(0.7f)) {
            const vec2 q = sp.box.center + u * a + v * b;
            const cell_info *c = m.cell_at(q);
            if (c && c->g == ground::lot)
              m.props.push_back({prop_kind::container, q, sp.box.angle, static_cast<f32>(r.range(1, 3)),
                                 r.next_u32()});
          }
      break;
    case spot_kind::parking:
      for (f32 a = -sp.box.half.x + 8.0f; a < sp.box.half.x - 6.0f; a += 7.0f)
        if (r.chance(0.6f)) {
          const vec2 q = sp.box.center + u * a + v * (sp.box.half.y * 0.5f);
          const cell_info *c = m.cell_at(q);
          if (c && c->g == ground::lot)
            m.props.push_back({prop_kind::motorbike, q, sp.box.angle + 90.0f, 1.0f, r.next_u32()});
        }
      break;
    case spot_kind::park:
    case spot_kind::vacant_lot: {
      const f32 area = sp.box.half.x * sp.box.half.y * 4.0f;
      const i32 n = std::min(60, static_cast<i32>(area / (sp.kind == spot_kind::park ? 1400.0f : 5000.0f)));
      for (i32 i = 0; i < n; ++i) {
        const vec2 q = sp.box.center + u * r.range(-sp.box.half.x, sp.box.half.x) +
                       v * r.range(-sp.box.half.y, sp.box.half.y);
        const cell_info *c = m.cell_at(q);
        if (c && (c->g == ground::park || c->g == ground::lot))
          m.props.push_back({r.chance(0.85f) ? prop_kind::tree : prop_kind::bench, q, r.range(0.0f, 360.0f),
                             r.range(0.8f, 1.3f), r.next_u32()});
      }
      break;
    }
    default:
      break;
    }
  }
  // Trees on the scraps of green and the embankment.
  for (i32 y = 0; y < m.rows; y += 3)
    for (i32 x = 0; x < m.cols; x += 3) {
      const cell_info &ci = m.at(x, y);
      if ((ci.g == ground::park && r.chance(0.12f)) || (ci.g == ground::plaza && ci.block < 0 && r.chance(0.05f)))
        m.props.push_back({prop_kind::tree, m.center_of(x, y) + vec2{r.range(-6.0f, 6.0f), r.range(-6.0f, 6.0f)},
                           r.range(0.0f, 360.0f), r.range(0.8f, 1.2f), r.next_u32()});
    }
  // Boats moored by the banks.
  if (m.river.on) {
    const polyline_walk walk(m.river.pts);
    for (f32 s = r.range(0.0f, 200.0f); s < walk.length(); s += r.range(90.0f, 260.0f)) {
      vec2 p, t;
      walk.at(s, p, t);
      const vec2 q = p + perp(t) * ((m.river.width * 0.5f - 14.0f) * (r.chance(0.5f) ? 1.0f : -1.0f));
      const cell_info *c = m.cell_at(q);
      if (c && c->g == ground::water)
        m.props.push_back({prop_kind::boat, q, angle_of(t) + r.range(-8.0f, 8.0f), r.range(0.8f, 1.3f), r.next_u32()});
    }
  }
  avoid_props();
}

namespace {

// The ground a prop stands on, as render_props.cpp draws it (world units).
// False for what stands in no one's way (boats).
bool prop_footprint(const prop &p, obb &out) {
  if (const street::asset *a = street::asset_for(p)) {
    out = street::footprint(p, *a);
    return true;
  }
  const f32 s = p.scale;
  vec2 half{};
  f32 angle = p.angle;
  switch (p.kind) {
  case prop_kind::tree: half = {1.0f * s, 1.0f * s}; break;
  case prop_kind::lamp: half = {0.5f, 0.5f}; break;
  case prop_kind::pole: half = {0.7f * s, 0.7f * s}; break;
  case prop_kind::motorbike: half = {3.5f, 1.1f}; break;
  case prop_kind::stool: half = {1.1f, 1.1f}; break;
  case prop_kind::stall: half = {5.5f, 3.5f}; break;
  case prop_kind::container: half = {15.0f, 5.5f}; break;
  case prop_kind::bench: half = {4.0f, 1.5f}; break;
  case prop_kind::monument: half = {10.0f, 10.0f}; angle = 0.0f; break;
  default: return false;
  }
  out = {p.pos, half, angle};
  return true;
}

} // namespace

// People on foot walk round what stands in their way, as the physics stops
// them at it: a cell whose middle is under a prop (widened by a person's
// half width) is closed, one it only reaches into costs more. The way in to
// every door stays open (nothing is parked or set out across it).
void generator::avoid_props() {
  constexpr f32 person_half_width = 1.8f;
  // The way in: the door's cell and the two beyond it, out to the street.
  std::vector<u8> door(m.cells.size(), 0);
  for (const building &b : m.buildings) {
    if (!b.door_ok)
      continue;
    for (i32 k = 0; k < 3; ++k) {
      const vec2 q = b.door + b.front() * (static_cast<f32>(k) * cs);
      const i32 x = static_cast<i32>(q.x / cs), y = static_cast<i32>(q.y / cs);
      if (x >= 0 && y >= 0 && x < m.cols && y < m.rows)
        door[static_cast<size_t>(y * m.cols + x)] = 1;
    }
  }
  // Whether closing (x, y) could cut the ways past it: its open neighbours,
  // walked round the ring of eight, fall into more than one run (a one-cell
  // alley, a gap between two stalls). Then it stays open and only costs more.
  const auto joins = [&](i32 x, i32 y) {
    static constexpr i32 ring[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}};
    bool open[8];
    for (i32 k = 0; k < 8; ++k) {
      const i32 nx = x + ring[k][0], ny = y + ring[k][1];
      open[k] = nx >= 0 && ny >= 0 && nx < m.cols && ny < m.rows &&
                m.foot.cost[static_cast<size_t>(ny * m.cols + nx)] > 0;
    }
    // Runs of open cells round the ring (a corner cell open alone is a run
    // of its own: no squeezing past a corner).
    i32 runs = 0;
    for (i32 k = 0; k < 8; ++k)
      if (open[k] && !open[(k + 7) % 8])
        ++runs;
    return runs > 1;
  };
  for (const prop &p : m.props) {
    obb o;
    if (!prop_footprint(p, o))
      continue;
    o.half += vec2{person_half_width, person_half_width};
    const f32 reach = length(o.half) + cs;
    const i32 x0 = std::max(0, static_cast<i32>((o.center.x - reach) / cs));
    const i32 x1 = std::min(m.cols - 1, static_cast<i32>((o.center.x + reach) / cs));
    const i32 y0 = std::max(0, static_cast<i32>((o.center.y - reach) / cs));
    const i32 y1 = std::min(m.rows - 1, static_cast<i32>((o.center.y + reach) / cs));
    for (i32 y = y0; y <= y1; ++y)
      for (i32 x = x0; x <= x1; ++x) {
        const size_t i = static_cast<size_t>(y * m.cols + x);
        const u8 cost = m.foot.cost[i];
        if (cost == 0)
          continue;
        const vec2 c = m.center_of(x, y);
        if (o.contains(c) && !door[i] && !joins(x, y)) {
          nav_set_cost(m.foot, {x, y}, 0);
          continue;
        }
        // Reaching into the cell: the cell's middle within half a cell of it.
        const obb wide{o.center, o.half + vec2{cs * 0.5f, cs * 0.5f}, o.angle};
        if (wide.contains(c))
          nav_set_cost(m.foot, {x, y}, static_cast<u8>(std::min(9, cost + 4)));
      }
  }
}

} // namespace sandtable::city
