#include "city.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace sandtable::city {

namespace {

bool boxes_overlap(const obb &a, const obb &b) {
  const vec2 axes[4] = {a.axis_x(), a.axis_y(), b.axis_x(), b.axis_y()};
  const vec2 d = b.center - a.center;
  for (const vec2 n : axes) {
    const f32 ra = a.half.x * std::fabs(dot(a.axis_x(), n)) + a.half.y * std::fabs(dot(a.axis_y(), n));
    const f32 rb = b.half.x * std::fabs(dot(b.axis_x(), n)) + b.half.y * std::fabs(dot(b.axis_y(), n));
    if (std::fabs(dot(d, n)) >= ra + rb - 1.0f)
      return false;
  }
  return true;
}

// Connected pieces of a nav grid (4-neighbours), -1 where it is blocked.
i32 components(const nav_grid &g, std::vector<i32> &comp, std::vector<i32> &sizes) {
  comp.assign(static_cast<size_t>(g.width * g.height), -1);
  sizes.clear();
  std::vector<i32> queue;
  for (i32 i = 0; i < g.width * g.height; ++i) {
    if (comp[static_cast<size_t>(i)] >= 0 || g.cost[static_cast<size_t>(i)] == 0)
      continue;
    const i32 id = static_cast<i32>(sizes.size());
    sizes.push_back(0);
    queue.assign(1, i);
    comp[static_cast<size_t>(i)] = id;
    for (size_t h = 0; h < queue.size(); ++h) {
      const i32 c = queue[h];
      ++sizes.back();
      const i32 x = c % g.width, y = c / g.width;
      const i32 nx[4] = {x + 1, x - 1, x, x};
      const i32 ny[4] = {y, y, y + 1, y - 1};
      for (i32 k = 0; k < 4; ++k) {
        if (nx[k] < 0 || ny[k] < 0 || nx[k] >= g.width || ny[k] >= g.height)
          continue;
        const i32 n = ny[k] * g.width + nx[k];
        if (comp[static_cast<size_t>(n)] < 0 && g.cost[static_cast<size_t>(n)] != 0) {
          comp[static_cast<size_t>(n)] = id;
          queue.push_back(n);
        }
      }
    }
  }
  return static_cast<i32>(sizes.size());
}

i32 largest(const std::vector<i32> &sizes) {
  i32 best = -1;
  for (i32 i = 0; i < static_cast<i32>(sizes.size()); ++i)
    if (best < 0 || sizes[static_cast<size_t>(i)] > sizes[static_cast<size_t>(best)])
      best = i;
  return best;
}

} // namespace

void validate(city_map &m) {
  city_report &rep = m.report;
  rep.errors.clear();
  rep.warnings.clear();
  char buf[256];
  auto error = [&](const char *fmt, auto... args) {
    std::snprintf(buf, sizeof(buf), fmt, args...);
    rep.errors.emplace_back(buf);
  };
  auto warn = [&](const char *fmt, auto... args) {
    std::snprintf(buf, sizeof(buf), fmt, args...);
    rep.warnings.emplace_back(buf);
  };

  // Buildings: inside the table, on no road or water, overlapping nothing.
  const f32 bucket = 64.0f;
  const i32 bw = static_cast<i32>(m.desc.width / bucket) + 2, bh = static_cast<i32>(m.desc.height / bucket) + 2;
  std::vector<std::vector<i32>> grid(static_cast<size_t>(bw * bh));
  i32 overlaps = 0, on_road = 0, outside = 0;
  for (i32 i = 0; i < static_cast<i32>(m.buildings.size()); ++i) {
    const building &b = m.buildings[static_cast<size_t>(i)];
    for (i32 k = 0; k < 4; ++k) {
      const vec2 c = b.box.corner(k);
      if (c.x < 0.0f || c.y < 0.0f || c.x > m.desc.width || c.y > m.desc.height)
        ++outside;
      const vec2 in = c + normalize(b.box.center - c) * 1.0f;
      const cell_info *ci = m.cell_at(in);
      if ((ci && ci->g == ground::water) || m.on_road(in, 1.0f))
        ++on_road;
    }
    const i32 gx = std::clamp(static_cast<i32>(b.box.center.x / bucket), 0, bw - 1);
    const i32 gy = std::clamp(static_cast<i32>(b.box.center.y / bucket), 0, bh - 1);
    for (i32 y = std::max(0, gy - 3); y <= std::min(bh - 1, gy + 3); ++y)
      for (i32 x = std::max(0, gx - 3); x <= std::min(bw - 1, gx + 3); ++x)
        for (const i32 o : grid[static_cast<size_t>(y * bw + x)])
          if (boxes_overlap(b.box, m.buildings[static_cast<size_t>(o)].box))
            ++overlaps;
    grid[static_cast<size_t>(gy * bw + gx)].push_back(i);
  }
  if (overlaps)
    error("%d building overlaps", overlaps);
  if (on_road)
    error("%d building corners on a road or water", on_road);
  if (outside)
    error("%d building corners off the table", outside);

  // Every business reachable on foot from the main network.
  std::vector<i32> comp, sizes;
  components(m.foot, comp, sizes);
  const i32 main_foot = largest(sizes);
  i32 cut_off = 0;
  for (const business &bz : m.businesses) {
    const cell_info *c = m.cell_at(bz.door);
    const i32 x = static_cast<i32>(bz.door.x / m.desc.cell), y = static_cast<i32>(bz.door.y / m.desc.cell);
    if (!c || !m.inside(x, y) || comp[static_cast<size_t>(y * m.cols + x)] != main_foot)
      ++cut_off;
  }
  if (cut_off)
    error("%d businesses not reachable on foot", cut_off);

  // The driving network in one piece.
  components(m.car, comp, sizes);
  i32 total = 0;
  for (const i32 s : sizes)
    total += s;
  const i32 main_car = largest(sizes);
  if (main_car < 0)
    error("no carriageway at all");
  else {
    const f32 share = static_cast<f32>(sizes[static_cast<size_t>(main_car)]) / static_cast<f32>(std::max(total, 1));
    if (share < 0.97f) {
      // Where the biggest piece cut off from the rest is, to go and look.
      i32 other = -1;
      for (i32 i = 0; i < static_cast<i32>(sizes.size()); ++i)
        if (i != main_car && (other < 0 || sizes[static_cast<size_t>(i)] > sizes[static_cast<size_t>(other)]))
          other = i;
      vec2 sum{};
      for (i32 i = 0; i < static_cast<i32>(comp.size()); ++i)
        if (comp[static_cast<size_t>(i)] == other)
          sum += m.center_of(i % m.cols, i / m.cols);
      sum /= static_cast<f32>(std::max(1, sizes[static_cast<size_t>(other)]));
      error("street network in pieces: main part %.1f%%, a %d-cell piece at (%.0f, %.0f)",
            static_cast<f64>(share * 100.0f), sizes[static_cast<size_t>(other)], static_cast<f64>(sum.x),
            static_cast<f64>(sum.y));
    }
    else if (sizes.size() > 1)
      warn("%d small carriageway islands", static_cast<i32>(sizes.size()) - 1);
  }

  // Blocks and districts worth playing on.
  for (i32 i = 0; i < static_cast<i32>(m.blocks.size()); ++i) {
    const block &b = m.blocks[static_cast<size_t>(i)];
    if (static_cast<f32>(b.cells) * m.desc.cell * m.desc.cell < 110.0f * 100.0f)
      error("block %d too small (%d cells)", i, b.cells);
    if (b.buildings.empty())
      warn("block %d has no buildings", i);
  }
  for (const district &d : m.districts) {
    if (d.blocks.empty())
      warn("district %s has no blocks", d.name.c_str());
    else {
      i32 biz = 0;
      for (const i32 b : d.blocks)
        biz += static_cast<i32>(m.blocks[static_cast<size_t>(b)].businesses.size());
      if (biz < 3)
        warn("district %s has only %d businesses", d.name.c_str(), biz);
    }
  }
  if (m.blocks.size() < 20)
    error("only %d blocks", static_cast<i32>(m.blocks.size()));
  if (m.businesses.size() < 60)
    error("only %d businesses", static_cast<i32>(m.businesses.size()));
  if (m.buildings.size() < 500)
    error("only %d buildings", static_cast<i32>(m.buildings.size()));
  if (m.hq_sites.size() < 4)
    error("only %d gang seats", static_cast<i32>(m.hq_sites.size()));
  for (const road_edge &e : m.edges)
    if (e.a < 0 || e.b < 0 || e.pts.size() < 2)
      error("broken road edge");
  if (rep.caps_hit)
    warn("%d searches stopped at their cap", rep.caps_hit);
  if (rep.gen_ms > 1500.0f)
    warn("slow: %.0f ms", static_cast<f64>(rep.gen_ms));
}

i32 run_city_check(u32 first, i32 count, bool verbose) {
  i32 failed = 0;
  f32 worst = 0.0f, sum = 0.0f;
  for (i32 i = 0; i < count; ++i) {
    const u32 seed = first + static_cast<u32>(i);
    city_desc desc;
    desc.seed = seed;
    city_map a, b;
    generate(a, desc);
    generate(b, desc);
    const bool same = a.hash == b.hash && city_hash(a) == a.hash;
    if (!same)
      a.report.errors.push_back("not reproducible: two runs differ");
    worst = std::max(worst, a.report.gen_ms);
    sum += a.report.gen_ms;
    const bool ok = a.report.ok();
    if (!ok)
      ++failed;
    if (verbose || !ok) {
      std::printf("[city] seed %u %s  %5.0f ms  roads %d  blocks %d  buildings %d  businesses %d  spots %d  props %d"
                  "  nodes %d\n",
                  seed, ok ? "ok  " : "FAIL", static_cast<f64>(a.report.gen_ms), static_cast<i32>(a.roads.size()),
                  static_cast<i32>(a.blocks.size()), static_cast<i32>(a.buildings.size()),
                  static_cast<i32>(a.businesses.size()), static_cast<i32>(a.spots.size()),
                  static_cast<i32>(a.props.size()), static_cast<i32>(a.nodes.size()));
      for (const std::string &e : a.report.errors)
        std::printf("         error: %s\n", e.c_str());
      if (verbose) {
        std::printf("         ");
        for (const city_report::stage_time &st : a.report.stages)
          std::printf("%s %.0f  ", st.name, static_cast<f64>(st.ms));
        std::printf("\n");
      }
      if (verbose)
        for (const std::string &w : a.report.warnings)
          std::printf("         warning: %s\n", w.c_str());
    }
  }
  std::printf("[city] %d of %d seeds ok, %.0f ms average, %.0f ms worst\n", count - failed, count,
              static_cast<f64>(sum / static_cast<f32>(std::max(count, 1))), static_cast<f64>(worst));
  return failed;
}

bool write_city_ppm(const city_map &m, const char *path) {
  std::FILE *f = std::fopen(path, "wb");
  if (!f)
    return false;
  std::fprintf(f, "P6\n%d %d\n255\n", m.cols, m.rows);
  static const u8 district_tint[][3] = {{210, 170, 120}, {220, 200, 110}, {190, 180, 160}, {210, 130, 170},
                                        {140, 170, 200}, {160, 150, 140}, {170, 200, 190}};
  for (i32 y = 0; y < m.rows; ++y)
    for (i32 x = 0; x < m.cols; ++x) {
      const cell_info &c = m.at(x, y);
      u8 px[3] = {0, 0, 0};
      auto set = [&](i32 r, i32 g, i32 b) {
        px[0] = static_cast<u8>(r);
        px[1] = static_cast<u8>(g);
        px[2] = static_cast<u8>(b);
      };
      const u8 *t = district_tint[static_cast<i32>(m.districts[c.district].kind)];
      switch (c.g) {
      case ground::free: set(t[0], t[1], t[2]); break;
      case ground::road:
        if (c.road == road_kind::alley)
          set(150, 140, 130);
        else if (c.carriage)
          set(60, 60, 66);
        else
          set(120, 120, 124);
        break;
      case ground::water: set(50, 90, 150); break;
      case ground::bridge: set(100, 90, 80); break;
      case ground::building: set(t[0] / 2, t[1] / 2, t[2] / 2); break;
      case ground::park: set(80, 150, 70); break;
      case ground::plaza: set(200, 200, 190); break;
      case ground::lot: set(170, 150, 110); break;
      }
      std::fwrite(px, 1, 3, f);
    }
  std::fclose(f);
  return true;
}

} // namespace sandtable::city
