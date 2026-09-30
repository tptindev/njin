#pragma once

#include "types.h"

namespace sandtable {

// The light of the sky at the current hour: white by day, orange at dawn and
// dusk, blue at night.
vec3 daylight();
// How dark it is, 0 in daylight to 1 in the deep night: torches burn from
// about 0.3.
f32 darkness();

} // namespace sandtable
