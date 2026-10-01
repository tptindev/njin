#pragma once

#include "city/city.h"
#include "types.h"

namespace sandtable {

// The town as the physics sees it (njin_physics3d.h, on Jolt Physics): the
// ground, every building and the street furniture that stands in the way
// (tree trunks, lamp and electricity posts, parked bikes, stools, stalls,
// containers, benches, the monument, bridge railings) as static bodies; each
// person on foot as a character that these and other people stop, sliding
// along them.
//
// The physics works in metres, the size Jolt is tuned for (the city's scale,
// city::units_per_metre). Height is up (y); table x and y are its x and z.

inline constexpr f32 metres_per_unit = 1.0f / city::units_per_metre;

inline vec3 to_phys(vec2 p, f32 height = 0.0f) {
  return {p.x * metres_per_unit, height * metres_per_unit, p.y * metres_per_unit};
}
inline vec2 from_phys(vec3 p) { return {p.x / metres_per_unit, p.z / metres_per_unit}; }

// The town's bodies, after each new city (they replace the last one's).
void physics_build(context &ctx, const city::city_map &map);
void physics_clear(context &ctx);
// Takes away building `i`'s box, when a body of its own (city/render_pbk.cpp:
// its walls with the door's opening) stands in for it; false if it had none.
bool physics_drop_building(context &ctx, i32 i);

// A person on foot, feet at `at`: a capsule as wide and tall as one.
character3d_handle physics_person(context &ctx, vec2 at);
// A person that stays put (sitting): something to walk round. Cleared with
// the town.
void physics_seated(context &ctx, vec2 at);

} // namespace sandtable
