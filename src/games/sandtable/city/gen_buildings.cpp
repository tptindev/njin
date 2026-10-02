#include "gen.h"

#include <algorithm>
#include <cmath>

// Stages 8, 10 and 11: landmarks, the houses along every road and inside
// the blocks, and the open places left over.

namespace sandtable::city {

std::vector<generator::front_spot> generator::frontage_in(i32 district_id, bool avenues_only, rng &r) const {
  std::vector<front_spot> out;
  for (i32 ri = 0; ri < static_cast<i32>(m.roads.size()); ++ri) {
    const road &rd = m.roads[static_cast<size_t>(ri)];
    if (rd.kind == road_kind::alley || (avenues_only && rd.kind != road_kind::avenue))
      continue;
    const polyline_walk walk(rd.pts);
    for (f32 s = 20.0f; s < walk.length() - 20.0f; s += 36.0f)
      for (const i32 side : {1, -1}) {
        vec2 p, t;
        walk.at(s, p, t);
        const cell_info *c = m.cell_at(p + perp(t) * (static_cast<f32>(side) * (rd.reach() + 14.0f)));
        if (c && c->g == ground::free && c->block >= 0 &&
            (district_id < 0 || m.blocks[static_cast<size_t>(c->block)].district == district_id))
          out.push_back({ri, s, side});
      }
  }
  for (i32 i = static_cast<i32>(out.size()) - 1; i > 0; --i)
    std::swap(out[static_cast<size_t>(i)], out[static_cast<size_t>(r.range(0, i))]);
  return out;
}

// Tries to put a `w` by `depth` box fronting a road of the district. With
// `apron`, an open space that deep is kept in front of it.
bool generator::place_landmark(i32 district_id, bool avenue, f32 w, f32 depth, f32 apron, obb &box, obb &front, rng &r, i32 &road_id) {
  const std::vector<front_spot> spots = frontage_in(district_id, avenue, r);
  i32 tries = 0;
  for (const front_spot &fs : spots) {
    if (++tries > 160)
      break;
    const road &rd = m.roads[static_cast<size_t>(fs.road)];
    const polyline_walk walk(rd.pts);
    obb all;
    if (!front_box(fs.road, walk, fs.s, fs.side, w, depth + apron, 8.4f, all))
      continue;
    const cell_info *c = m.cell_at(all.center);
    if (!c || !fits(all, c->block))
      continue;
    // Split into the apron at the front and the building behind.
    const vec2 back = all.axis_y();
    front = {all.center - back * (depth * 0.5f), {w * 0.5f, apron * 0.5f}, all.angle};
    box = {all.center + back * (apron * 0.5f), {w * 0.5f, depth * 0.5f}, all.angle};
    road_id = fs.road;
    return true;
  }
  return false;
}

void generator::make_landmarks() {
  rng r = stage_rng(8);
  for (i32 di = 0; di < static_cast<i32>(m.districts.size()); ++di) {
    const district_kind k = m.districts[static_cast<size_t>(di)].kind;
    obb box, apron;
    i32 rid = -1;
    auto building_at = [&](building_kind kind, bool avenue, f32 w0, f32 w1, f32 d0, f32 d1, f32 ap, i32 f0, i32 f1,
                           spot_kind apron_kind) {
      const f32 w = in_bays(r.range(w0, w1), 4), dd = in_bays(r.range(d0, d1), 4);
      if (!place_landmark(di, avenue, w, dd, ap, box, apron, r, rid))
        return -1;
      if (ap > 0.0f)
        add_spot(apron, apron_kind, apron_kind == spot_kind::market_square ? ground::plaza : ground::lot);
      return add_building(box, kind, r.range(f0, f1), rid, r.next_u32());
    };
    auto open_at = [&](spot_kind kind, f32 w0, f32 w1, f32 d0, f32 d1, ground g) {
      const f32 w = r.range(w0, w1), dd = r.range(d0, d1);
      if (place_landmark(di, false, w, dd, 0.0f, box, apron, r, rid))
        add_spot(box, kind, g);
    };
    switch (k) {
    case district_kind::market:
      building_at(building_kind::market_hall, false, 130.0f, 180.0f, 90.0f, 120.0f, 50.0f, 1, 2,
                  spot_kind::market_square);
      break;
    case district_kind::old_quarter:
      building_at(building_kind::pagoda, false, 70.0f, 90.0f, 60.0f, 80.0f, 40.0f, 1, 2, spot_kind::vacant_lot);
      break;
    case district_kind::docks:
      for (i32 i = r.range(3, 5); i > 0; --i)
        building_at(building_kind::warehouse, false, 100.0f, 150.0f, 70.0f, 96.0f, 0.0f, 2, 3,
                    spot_kind::vacant_lot);
      for (i32 i = r.range(1, 2); i > 0; --i)
        open_at(spot_kind::container_yard, 120.0f, 180.0f, 90.0f, 130.0f, ground::lot);
      break;
    case district_kind::industrial:
      for (i32 i = r.range(3, 5); i > 0; --i)
        building_at(building_kind::workshop, false, 80.0f, 130.0f, 60.0f, 96.0f, 30.0f, 1, 2,
                    spot_kind::vacant_lot);
      break;
    case district_kind::residential:
      if (r.chance(0.6f))
        building_at(building_kind::school, false, 110.0f, 150.0f, 60.0f, 72.0f, 50.0f, 2, 3,
                    spot_kind::sports_field);
      else
        open_at(spot_kind::sports_field, 100.0f, 130.0f, 60.0f, 80.0f, ground::park);
      // Reserve wing-house plots before frontage houses consume them.
      for (i32 i = 0; i < 2; ++i)
        building_at(building_kind::house, false, 108.0f, 120.0f, 84.0f, 108.0f, 0.0f, 2, 3,
                    spot_kind::vacant_lot);
      break;
    case district_kind::new_urban:
      for (i32 i = r.range(2, 4); i > 0; --i)
        building_at(building_kind::apartment, false, 72.0f, 96.0f, 72.0f, 96.0f, 0.0f, 6, max_floors,
                    spot_kind::vacant_lot);
      open_at(spot_kind::parking, 90.0f, 130.0f, 60.0f, 80.0f, ground::lot);
      building_at(building_kind::house, false, 108.0f, 120.0f, 84.0f, 108.0f, 0.0f, 2, 3,
                  spot_kind::vacant_lot);
      break;
    case district_kind::nightlife:
      building_at(building_kind::hotel, false, 60.0f, 72.0f, 72.0f, 84.0f, 0.0f, 5, max_floors, spot_kind::vacant_lot);
      open_at(spot_kind::parking, 70.0f, 100.0f, 50.0f, 70.0f, ground::lot);
      break;
    default:
      break;
    }
  }
  // A petrol station or two on the avenues.
  for (i32 i = r.range(1, 2); i > 0; --i) {
    obb box, apron;
    i32 rid = -1;
    if (place_landmark(-1, true, 60.0f, 24.0f, 30.0f, box, apron, r, rid)) {
      add_spot(apron, spot_kind::parking, ground::lot);
      const i32 b = add_building(box, building_kind::workshop, 1, rid, r.next_u32());
      m.buildings[static_cast<size_t>(b)].business = -2; // marked: becomes the station
    }
  }
}

void generator::front_houses(i32 road_id, rng &r) {
  const road rd = m.roads[static_cast<size_t>(road_id)];
  const polyline_walk walk(rd.pts);
  const bool alley = rd.kind == road_kind::alley;
  for (const i32 side : {1, -1}) {
    i32 count = 0;
    f32 s = alley ? 10.0f : 0.0f;
    for (i32 guard = 0; s < walk.length() && guard < 4000; ++guard) {
      vec2 p, t;
      walk.at(s, p, t);
      const cell_info *probe = m.cell_at(p + perp(t) * (static_cast<f32>(side) * (rd.reach() + 12.0f)));
      if (!probe || probe->block < 0 || probe->g != ground::free) {
        s += 6.0f;
        continue;
      }
      const district_kind dk = m.districts[static_cast<size_t>(m.blocks[static_cast<size_t>(probe->block)].district)].kind;
      const profile &pf = prof(dk);
      building_kind kind = building_kind::tube_house;
      f32 w, d0, d1;
      i32 f0 = pf.floors_min, f1 = pf.floors_max;
      // Lots in whole bays of the building kit, wide and deep enough for
      // its rules: a nhà ống three bays and more, seven deep; flats and
      // halls two rows of rooms and a corridor deep.
      if (alley) {
        kind = building_kind::house;
        w = in_bays(r.range(36.0f, 48.0f), 3);
        d0 = 84.0f;
        d1 = in_bays(r.range(84.0f, 108.0f), 7);
        f0 = 1;
        f1 = 3;
      } else if (dk == district_kind::new_urban && r.chance(0.4f)) {
        kind = building_kind::apartment;
        w = in_bays(r.range(60.0f, 96.0f), 5);
        d0 = 60.0f;
        d1 = in_bays(r.range(72.0f, 96.0f), 6);
        f0 = 5;
        f1 = max_floors;
      } else if ((dk == district_kind::docks || dk == district_kind::industrial) && r.chance(0.5f)) {
        kind = building_kind::workshop;
        w = in_bays(r.range(48.0f, 96.0f), 4);
        d0 = 60.0f;
        d1 = in_bays(r.range(72.0f, 108.0f), 6);
        f0 = 1;
        f1 = 2;
      } else if (dk == district_kind::nightlife && r.chance(0.1f)) {
        kind = building_kind::hotel;
        w = in_bays(r.range(48.0f, 72.0f), 4);
        d0 = 60.0f;
        d1 = in_bays(r.range(60.0f, 84.0f), 5);
        f0 = 4;
        f1 = max_floors;
      } else {
        w = in_bays(std::max(r.range(pf.front_min, pf.front_max), 36.0f), 3);
        if (r.chance(0.18f)) w = in_bays(r.range(60.0f, 84.0f), 5);
        d0 = dk == district_kind::residential || dk == district_kind::new_urban ? 84.0f : 96.0f;
        d1 = in_bays(std::max(r.range(pf.depth_min, pf.depth_max), 108.0f), 9);
        f1 = std::min(f1, 5); // the tube house's rules: up to five floors
        f0 = std::min(f0, f1);
      }
      bool placed = false;
      // Try progressively narrower and shallower legal lots before giving
      // up a frontage, instead of abandoning every gap below the first roll.
      f32 used_width = w;
      const f32 min_width = kind == building_kind::tube_house ? 36.0f :
          kind == building_kind::house ? 36.0f : 48.0f;
      for (f32 width = w; width >= min_width && !placed; width -= bay_w)
        for (f32 depth = d1; depth >= d0 - 0.01f && !placed; depth -= bay_w) {
          obb box;
          if (!front_box(road_id, walk, s, side, width, depth, 8.4f, box)) continue;
          const cell_info *c = m.cell_at(box.center);
          if (!c || !fits(box, c->block)) continue;
          const i32 b = add_building(box, kind, r.range(f0, f1), road_id, r.next_u32());
          building &bd = m.buildings[static_cast<size_t>(b)];
          bd.number = ++count * 2 - (side > 0 ? 1 : 0);
          used_width = width;
          placed = true;
        }
      if (placed)
        s += used_width + (r.chance(alley ? 0.12f : 0.04f) ? r.range(6.0f, 16.0f) : 0.0f);
      else
        s += 6.0f;
    }
  }
}

void generator::make_houses() {
  rng r = stage_rng(10);
  for (const road_kind pass : {road_kind::avenue, road_kind::ring, road_kind::street, road_kind::alley})
    for (i32 i = 0; i < static_cast<i32>(m.roads.size()); ++i)
      if (m.roads[static_cast<size_t>(i)].kind == pass)
        front_houses(i, r);

  for (i32 bi = 0; bi < static_cast<i32>(m.blocks.size()); ++bi)
    fill_interior(bi, r);

  // A finer second pass fills compact usable gaps missed by random rows.
  // Reserve a walking apron around every infill plot; keep industrial yards.
  for (i32 bi = 0; bi < static_cast<i32>(m.blocks.size()); ++bi) {
    const block &bk = m.blocks[static_cast<size_t>(bi)];
    const auto dk = m.districts[static_cast<size_t>(bk.district)].kind;
    if (dk == district_kind::industrial || dk == district_kind::docks) continue;
    i32 infill = 0;
    for (f32 y = bk.bounds.pos.y + 36; y < bk.bounds.pos.y + bk.bounds.size.y - 36 && infill < 8; y += bay_w)
      for (f32 x = bk.bounds.pos.x + 36; x < bk.bounds.pos.x + bk.bounds.size.x - 36 && infill < 8; x += bay_w) {
        const vec2 at{x, y};
        const cell_info *c = m.cell_at(at);
        if (!c || c->block != bi || c->g != ground::free) continue;
        if (!fits({at, {36.0f, 39.0f}, bk.axis}, bi)) continue;
        add_building({at, {30.0f, 30.0f}, bk.axis}, building_kind::house, 1, -1, r.next_u32());
        ++infill;
      }
  }
}

// Inside a block, behind the street fronts: rows of whatever the district
// builds, square to the block, packed wall to wall with the odd yard left.
void generator::fill_interior(i32 bi, rng &r) {
  const block &bk = m.blocks[static_cast<size_t>(bi)];
  const district_kind dk = m.districts[static_cast<size_t>(bk.district)].kind;
  building_kind kind = building_kind::house;
  // Small houses standing free, a garden width apart: the detached house's
  // five bays and more.
  f32 w0 = 60.0f, w1 = 84.0f, d0 = 60.0f, d1 = 84.0f, gap = 6.0f, row_gap = 10.0f, skip = 0.04f;
  i32 f0 = 1, f1 = 3;
  switch (dk) {
  case district_kind::docks:
    kind = building_kind::warehouse;
    w0 = 60.0f, w1 = 120.0f, d0 = 72.0f, d1 = 96.0f, gap = 10.0f, row_gap = 14.0f, skip = 0.08f;
    f0 = 1, f1 = 2;
    break;
  case district_kind::industrial:
    kind = building_kind::workshop;
    w0 = 48.0f, w1 = 96.0f, d0 = 72.0f, d1 = 96.0f, gap = 8.0f, row_gap = 12.0f, skip = 0.08f;
    f0 = 1, f1 = 2;
    break;
  case district_kind::new_urban:
    kind = building_kind::apartment;
    w0 = 60.0f, w1 = 96.0f, d0 = 72.0f, d1 = 96.0f, gap = 18.0f, row_gap = 20.0f, skip = 0.12f;
    f0 = 5, f1 = max_floors;
    break;
  case district_kind::nightlife:
  case district_kind::market:
    skip = 0.06f;
    break;
  default:
    break;
  }
  // Some generous interior plots support true rear wings rather than just
  // rectangular frontage houses. Smaller candidates then fill the leftovers.
  const bool varied_houses = kind == building_kind::house;
  const vec2 u = from_angle(bk.axis), v = perp(u);
  const f32 ext = length(bk.bounds.size) * 0.5f + 20.0f;
  const vec2 c = rect_center(bk.bounds);
  for (f32 b = -ext; b < ext;) {
    const f32 depth = in_bays(varied_houses && r.chance(0.22f) ? r.range(84.0f, 108.0f) : r.range(d0, d1), 1);
    for (f32 a = -ext; a < ext;) {
      const f32 w = in_bays(varied_houses && depth >= 84.0f && r.chance(0.3f) ? r.range(84.0f, 120.0f) : r.range(w0, w1), 1);
      const vec2 p = c + u * (a + w * 0.5f) + v * (b + depth * 0.5f);
      const cell_info *ci = m.cell_at(p);
      if (!ci || ci->block != bi || ci->g != ground::free) {
        a += 6.0f;
        continue;
      }
      bool placed = false;
      f32 used_width = w;
      const f32 min_width = kind == building_kind::house ? 60.0f : w0;
      for (f32 width = w; width >= min_width && !placed; width -= bay_w)
        for (f32 dep = depth; dep >= d0 && !placed; dep -= bay_w) {
          const vec2 at = c + u * (a + width * 0.5f) + v * (b + dep * 0.5f);
          const obb box{at, {width * 0.5f, dep * 0.5f}, bk.axis};
          if (!fits(box, bi)) continue;
          if (!r.chance(skip)) add_building(box, kind, r.range(f0, f1), -1, r.next_u32());
          placed = true; used_width = width;
        }
      a += placed ? used_width + gap : 6.0f;
    }
    b += depth + row_gap;
  }
}

void generator::make_open_spots() {
  rng r = stage_rng(11);
  std::vector<region> regions;
  label_free(regions);
  for (const region &rg : regions) {
    if (rg.cells.size() < 40)
      continue; // a courtyard, a gap between houses
    const i32 blk = m.cells[static_cast<size_t>(rg.cells.front())].block;
    if (blk < 0)
      continue;
    const shape_stats st = stats(rg.cells);
    // A strip or a maze of gaps between houses stays a yard; only a
    // compact open piece is a place.
    const f32 fill = static_cast<f32>(rg.cells.size()) * cs * cs / std::max(1.0f, st.len() * st.wid());
    if (fill < 0.55f || st.wid() < 36.0f)
      continue;
    const district_kind dk = m.districts[static_cast<size_t>(m.blocks[static_cast<size_t>(blk)].district)].kind;
    spot_kind kind = spot_kind::vacant_lot;
    ground g = ground::lot;
    switch (dk) {
    case district_kind::docks:
      kind = rg.cells.size() > 150 ? spot_kind::container_yard : spot_kind::vacant_lot;
      break;
    case district_kind::new_urban:
    case district_kind::nightlife:
      kind = spot_kind::parking;
      break;
    case district_kind::residential:
      if (r.chance(0.3f)) {
        kind = spot_kind::park;
        g = ground::park;
      }
      break;
    default:
      break;
    }
    const vec2 u = from_angle(st.angle);
    const vec2 mid = st.c + u * ((st.umin + st.umax) * 0.5f) + perp(u) * ((st.vmin + st.vmax) * 0.5f);
    spot s;
    s.kind = kind;
    s.box = {mid, {st.len() * 0.5f, st.wid() * 0.5f}, st.angle};
    s.block = blk;
    s.district = m.blocks[static_cast<size_t>(blk)].district;
    m.spots.push_back(s);
    for (const i32 c : rg.cells)
      m.cells[static_cast<size_t>(c)].g = g;
  }
}

} // namespace sandtable::city
