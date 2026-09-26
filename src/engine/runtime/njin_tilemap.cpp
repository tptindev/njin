#include "_tilemap.h"
#include <cctype>
#include <string>

namespace njin {
bool tile_shape_from_name(const char *name, tile_shape &out) {
  if (name == nullptr)
    return false;
  std::string n;
  for (const char *c = name; *c != '\0'; c++) {
    const char ch = *c == '-' || *c == ' ' ? '_' : (char)std::tolower((unsigned char)*c);
    n.push_back(ch);
  }
  struct entry {
    const char *name;
    tile_shape shape;
  };
  static constexpr entry names[] = {
      {"solid", tile_solid},           {"wall", tile_solid},
      {"none", tile_none},             {"empty", tile_none},
      {"one_way", tile_one_way},       {"oneway", tile_one_way},
      {"platform", tile_one_way},      {"slope_r", tile_slope_r},
      {"slope_l", tile_slope_l},       {"slope_r_low", tile_slope_r_low},
      {"slope_r_high", tile_slope_r_high}, {"slope_l_low", tile_slope_l_low},
      {"slope_l_high", tile_slope_l_high},
  };
  for (const entry &e : names) {
    if (n == e.name) {
      out = e.shape;
      return true;
    }
  }
  return false;
}
} // namespace njin
