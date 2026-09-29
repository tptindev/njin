#pragma once

#include "types.h"

namespace sandtable {

// Clouds drifting over the table and rain, from the level's weather. They do
// not keep any state: where a cloud or a raindrop is follows from the time.

// Clouds and their shadows, in world space, over the troops.
void draw_clouds(context &ctx);
// Rain streaks, splashes and the grey of a wet day, in screen pixels, under
// the HUD.
void draw_rain(context &ctx);

// How much bows and guns lose to the rain: 1 dry, less in rain.
f32 rain_reach();

// The light of the sky at the current hour, dimmed by rain: white by day,
// orange at dawn and dusk, blue at night.
vec3 daylight();
// How dark it is, 0 in daylight to 1 in the deep night: torches burn from
// about 0.3.
f32 darkness();

} // namespace sandtable
