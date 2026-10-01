#pragma once

#include "city.h"

#include <string>
#include <vector>

// The street furniture kit (assets/models/street_clay): lamps, poles,
// benches, plastic stools and chairs, the street-food carts, as clay GLBs in
// metres, +Y up, the front +Z. The town's props of those kinds are drawn
// from it at their real size, and their footprints (what people on foot walk
// round, what the physics stops them at) come from its manifest. Data only,
// no context: the generator reads it too.

namespace sandtable::city::street {

inline constexpr const char *kit_dir = "assets/models/street_clay";

struct asset {
  std::string id, path;
  vec3 size{};       // metres: along the asset's +X, up, along its depth
  vec2 foot_c{};     // the ground footprint's middle, metres in the asset's own x, z (glTF)
  vec2 foot_half{};  // and its half sizes
  f32 height = 0.0f; // metres
  bool post = false; // a lamp or a pole: only its post stands on the ground, the arms are overhead
  vec3 light{};      // a lamp's bulb, metres in the asset's own x, y, z (glTF)
};

// The kit's manifest, read once; empty when it did not load.
const std::vector<asset> &assets();
const asset *find(const std::string &id);

// The asset a prop of the town is drawn with, picked by its look among its
// group's (distribution_rules.json weights), or null for the kinds the kit
// has not got (trees, motorbikes, containers, boats, the monument).
const asset *asset_for(const prop &p);

// Where an asset's ground footprint lies for prop `p`, world units: its
// +X along `p.angle`.
obb footprint(const prop &p, const asset &a);

} // namespace sandtable::city::street
