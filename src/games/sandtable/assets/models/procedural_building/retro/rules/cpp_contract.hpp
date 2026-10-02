#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Lightweight consumer types/helpers. JSON parsing and runtime integration are left to the game.
namespace sandtable::building_rules {
struct point2 { double x = 0, y = 0; };
struct point3 { double x = 0, y = 0, z = 0; };
using polygon = std::vector<point2>;
struct room {
  std::string id, type;
  polygon net_polygon_m;
  double net_area_m2 = 0;
};
struct portal {
  std::string id, from_room, to_room, kind, module_id;
  int floor_from = 0, floor_to = 0;
  point2 center_m, normal;
  double aperture_width_m = 0, net_clear_width_m = 0, height_m = 0;
};
struct floor_plan {
  int level = 0;
  double elevation_m = 0;
  std::vector<room> rooms;
  std::vector<polygon> slab_voids_m;
};
struct building_plan {
  std::uint32_t seed = 0;
  int rule_version = 1;
  std::string archetype, style, kit_revision;
  double width_m = 0, depth_m = 0;
  polygon outer_m;
  std::vector<polygon> holes_m;
  std::vector<floor_plan> floors;
  std::vector<portal> portals;
};

// Matches city/render_common.cpp::mix, with well-defined unsigned wraparound.
constexpr std::uint32_t sub_seed(std::uint32_t seed, std::uint32_t salt) {
  auto h = seed ^ (salt * 0x9E3779B9u);
  h ^= h >> 16; h *= 0x7feb352du;
  h ^= h >> 15; h *= 0x846ca68bu;
  return h ^ (h >> 16);
}
inline point2 local_to_table(point2 local, point2 center, double width_m,
                             double depth_m, double building_angle_degrees) {
  const double a = building_angle_degrees * 3.14159265358979323846 / 180.0;
  const double u = (local.x - width_m * .5) * 6.0;
  const double v = (local.y - depth_m * .5) * 6.0;
  return {center.x + std::cos(a)*u - std::sin(a)*v,
          center.y + std::sin(a)*u + std::cos(a)*v};
}
// Equivalent to to3d(table_xy, elevation_m * units_per_metre * unit3d).
inline point3 table_to_render(point2 table, double elevation_m) {
  return {table.x / 32.0, elevation_m * 6.0 / 32.0, table.y / 32.0};
}
// Convert Blender model coordinates (X width, Y depth, Z up) to Y-up engine axes.
inline point3 blender_local_to_y_up(point3 p) { return {p.x, p.z, -p.y}; }
// Instance orientation must subsequently align local depth with box.axis_y().
// Use the existing renderer's basis/yaw convention, not a second sign flip.
}
