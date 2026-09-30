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
    if (!front_box(fs.road, walk, fs.s, fs.side, w, depth + apron, 3.0f, all))
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
      const f32 w = r.range(w0, w1), dd = r.range(d0, d1);
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
        building_at(building_kind::school, false, 110.0f, 150.0f, 44.0f, 56.0f, 50.0f, 2, 3,
                    spot_kind::sports_field);
      else
        open_at(spot_kind::sports_field, 100.0f, 130.0f, 60.0f, 80.0f, ground::park);
      break;
    case district_kind::new_urban:
      for (i32 i = r.range(2, 4); i > 0; --i)
        building_at(building_kind::apartment, false, 64.0f, 90.0f, 56.0f, 76.0f, 0.0f, 10, 16,
                    spot_kind::vacant_lot);
      open_at(spot_kind::parking, 90.0f, 130.0f, 60.0f, 80.0f, ground::lot);
      break;
    case district_kind::nightlife:
      building_at(building_kind::hotel, false, 50.0f, 70.0f, 56.0f, 76.0f, 0.0f, 7, 11, spot_kind::vacant_lot);
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
      if (alley) {
        kind = building_kind::house;
        w = r.range(22.0f, 34.0f);
        d0 = 26.0f;
        d1 = r.range(34.0f, 48.0f);
        f0 = 1;
        f1 = 3;
      } else if (dk == district_kind::new_urban && r.chance(0.4f)) {
        kind = building_kind::apartment;
        w = r.range(56.0f, 84.0f);
        d0 = 44.0f;
        d1 = r.range(50.0f, 72.0f);
        f0 = 7;
        f1 = 13;
      } else if ((dk == district_kind::docks || dk == district_kind::industrial) && r.chance(0.5f)) {
        kind = building_kind::workshop;
        w = r.range(48.0f, 84.0f);
        d0 = 44.0f;
        d1 = r.range(50.0f, 90.0f);
        f0 = 1;
        f1 = 2;
      } else if (dk == district_kind::nightlife && r.chance(0.1f)) {
        kind = building_kind::hotel;
        w = r.range(44.0f, 60.0f);
        d0 = 44.0f;
        d1 = r.range(50.0f, 76.0f);
        f0 = 6;
        f1 = 10;
      } else {
        w = r.range(pf.front_min, pf.front_max);
        d0 = std::min(pf.depth_min, 40.0f);
        d1 = r.range(pf.depth_min, pf.depth_max);
      }
      bool placed = false;
      for (f32 depth = d1; depth >= d0 - 0.01f && !placed; depth -= std::max(8.0f, (d1 - d0) * 0.34f)) {
        obb box;
        if (!front_box(road_id, walk, s, side, w, depth, 1.0f, box))
          break;
        const cell_info *c = m.cell_at(box.center);
        if (!c || !fits(box, c->block))
          continue;
        const i32 b = add_building(box, kind, r.range(f0, f1), road_id, r.next_u32());
        building &bd = m.buildings[static_cast<size_t>(b)];
        bd.number = ++count * 2 - (side > 0 ? 1 : 0);
        placed = true;
      }
      if (placed)
        s += w + (r.chance(alley ? 0.2f : 0.08f) ? r.range(6.0f, 16.0f) : 0.0f);
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
}

// Inside a block, behind the street fronts: rows of whatever the district
// builds, square to the block, packed wall to wall with the odd yard left.
void generator::fill_interior(i32 bi, rng &r) {
  const block &bk = m.blocks[static_cast<size_t>(bi)];
  const district_kind dk = m.districts[static_cast<size_t>(bk.district)].kind;
  building_kind kind = building_kind::house;
  f32 w0 = 22.0f, w1 = 36.0f, d0 = 24.0f, d1 = 40.0f, gap = 0.0f, row_gap = 2.0f, skip = 0.12f;
  i32 f0 = 1, f1 = 3;
  switch (dk) {
  case district_kind::docks:
    kind = building_kind::warehouse;
    w0 = 60.0f, w1 = 110.0f, d0 = 44.0f, d1 = 70.0f, gap = 10.0f, row_gap = 14.0f, skip = 0.2f;
    f0 = 1, f1 = 2;
    break;
  case district_kind::industrial:
    kind = building_kind::workshop;
    w0 = 48.0f, w1 = 90.0f, d0 = 40.0f, d1 = 64.0f, gap = 8.0f, row_gap = 12.0f, skip = 0.25f;
    f0 = 1, f1 = 2;
    break;
  case district_kind::new_urban:
    kind = building_kind::apartment;
    w0 = 44.0f, w1 = 70.0f, d0 = 40.0f, d1 = 56.0f, gap = 18.0f, row_gap = 20.0f, skip = 0.3f;
    f0 = 5, f1 = 10;
    break;
  case district_kind::nightlife:
  case district_kind::market:
    skip = 0.18f;
    break;
  default:
    break;
  }
  const vec2 u = from_angle(bk.axis), v = perp(u);
  const f32 ext = length(bk.bounds.size) * 0.5f + 20.0f;
  const vec2 c = rect_center(bk.bounds);
  for (f32 b = -ext; b < ext;) {
    const f32 depth = r.range(d0, d1);
    for (f32 a = -ext; a < ext;) {
      const f32 w = r.range(w0, w1);
      const vec2 p = c + u * (a + w * 0.5f) + v * (b + depth * 0.5f);
      const cell_info *ci = m.cell_at(p);
      if (!ci || ci->block != bi || ci->g != ground::free) {
        a += 6.0f;
        continue;
      }
      const obb box{p, {w * 0.5f, depth * 0.5f}, bk.axis};
      if (fits(box, bi)) {
        if (!r.chance(skip))
          add_building(box, kind, r.range(f0, f1), -1, r.next_u32());
        a += w + gap;
      } else {
        a += 6.0f;
      }
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
