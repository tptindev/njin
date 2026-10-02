#include "pbk_render.h"
#include "../clock.h"

#include <algorithm>
#include <cmath>

namespace sandtable::city::pbk {

namespace {
const json_value &light_rules() {
  static const json_value rules = [] {
    json_value j;
    if (!json_load(kit_path("rules/interior_lighting.json").c_str(), j))
      NJIN_WARN("pbk: missing interior_lighting.json; using default room lighting");
    return j;
  }();
  return rules;
}
}

f32 lighting_budget(const char *name, f32 fallback) {
  return static_cast<f32>(light_rules()["runtime_budget"][name].number_or(fallback));
}
f32 glazing_option(const char *name, f32 fallback) {
  return static_cast<f32>(light_rules()["glass_lod"][name].number_or(fallback));
}
bool clear_glass_at(vec3 center, const view_options &opt) {
  const f32 radius = std::clamp(glazing_option("clear_radius_m", 8), 1.0f, 15.0f) * units_per_metre * unit3d;
  return opt.camera != camera_mode::observation && opt.eye_position_valid &&
         length_sq(center - opt.eye_position) <= radius * radius;
}

std::vector<room_light> design_room_lights(const plan &p) {
  const json_value &rules = light_rules();
  const auto &placement = rules["placement"];
  const f32 grid = std::clamp(static_cast<f32>(placement["grid_m"].number_or(0.5)), 0.25f, 2.0f);
  const f32 clearance = std::clamp(static_cast<f32>(placement["wall_clearance_m"].number_or(0.25)), 0.05f, 1.0f);
  const f32 height = storey - std::clamp(static_cast<f32>(placement["ceiling_offset_m"].number_or(0.3)), 0.1f, 1.0f);
  const f32 area = std::max(4.0f, static_cast<f32>(placement["area_per_fixture_m2"].number_or(12)));
  const i32 max_count = std::clamp(static_cast<i32>(placement["max_fixtures_per_room"].number_or(4)), 1, 8);
  std::vector<room_light> result;
  for (const floor_plan &fl : p.floors)
    for (const room &r : fl.rooms) {
      if (r.poly.empty())
        continue;
      const auto &profiles = rules["profiles"];
      const auto &profile = profiles.has(r.type) ? profiles[r.type] : profiles["default"];
      box2 bounds{1e9f, 1e9f, -1e9f, -1e9f};
      for (const vec2 q : r.poly) {
        bounds.x0 = std::min(bounds.x0, q.x); bounds.y0 = std::min(bounds.y0, q.y);
        bounds.x1 = std::max(bounds.x1, q.x); bounds.y1 = std::max(bounds.y1, q.y);
      }
      const vec2 mid{(bounds.x0 + bounds.x1) * 0.5f, (bounds.y0 + bounds.y1) * 0.5f};
      std::vector<vec2> candidates, chosen;
      for (f32 y = bounds.y0 + grid * 0.5f; y < bounds.y1; y += grid)
        for (f32 x = bounds.x0 + grid * 0.5f; x < bounds.x1; x += grid) {
          const vec2 q{x, y};
          if (!point_in_polygon(r.poly, q) || distance_to_outline(r.poly, q) < clearance)
            continue;
          bool void_here = false;
          for (const polygon &v : fl.voids)
            if (!v.empty() && point_in_polygon(v, q)) void_here = true;
          if (!void_here) candidates.push_back(q);
        }
      const i32 count = std::clamp(static_cast<i32>(std::ceil(polygon_area(r.poly) / area)), 1, max_count);
      for (i32 k = 0; k < count && !candidates.empty(); ++k) {
        size_t best = 0;
        f32 best_score = -1e30f;
        for (size_t i = 0; i < candidates.size(); ++i) {
          f32 score = chosen.empty() ? -length_sq(candidates[i] - mid) : 1e30f;
          for (const vec2 q : chosen) score = std::min(score, length_sq(candidates[i] - q));
          if (score > best_score) { best_score = score; best = i; }
        }
        const vec2 q = candidates[best];
        chosen.push_back(q);
        candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(best));
        room_light l;
        l.room_id = r.id; l.floor = fl.level;
        l.local = {q.x, q.y, fl.elevation + height};
        l.color = {static_cast<f32>(profile["rgb"][0].number_or(1)),
                   static_cast<f32>(profile["rgb"][1].number_or(0.82)),
                   static_cast<f32>(profile["rgb"][2].number_or(0.62)), 1};
        l.intensity = std::clamp(static_cast<f32>(profile["intensity"].number_or(1.1)), 0.0f, 3.0f);
        // A ceiling fixture must reach the floor, even in a small bathroom.
        l.radius = std::clamp(static_cast<f32>(profile["radius_m"].number_or(3.5)), height + 0.8f, 6.0f);
        l.from = static_cast<f32>(profile["hours"][0].number_or(18));
        l.to = static_cast<f32>(profile["hours"][1].number_or(6));
        l.day_factor = std::clamp(static_cast<f32>(profile["day_factor"].number_or(0)), 0.0f, 1.0f);
        result.push_back(l);
      }
    }
  return result;
}

f32 room_light_power(const room_light &l, f32 hour, f32 night) {
  if (!hour_between(hour, l.from, l.to))
    return 0;
  return l.intensity * std::max(l.day_factor, smooth_fade(0.15f, 0.55f, night));
}

} // namespace sandtable::city::pbk
