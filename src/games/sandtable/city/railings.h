#pragma once
#include "city.h"

namespace sandtable::city {
struct railing_edge {
  vec2 a{}, b{}; // world units; shared by rendering and collision
  bool bridge = false;
};
inline constexpr f32 railing_height = 1.1f * units_per_metre;
inline constexpr f32 railing_thickness = 0.12f * units_per_metre;
std::vector<railing_edge> railing_layout(const city_map &map);
i32 run_railing_check(i32 seeds);
} // namespace sandtable::city
