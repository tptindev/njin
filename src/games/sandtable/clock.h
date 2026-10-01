#pragma once

#include "city/city.h"
#include "types.h"

namespace sandtable {

// The town's clock. Everything that lives in the town keeps to it: the
// townsfolk get up, go to work, eat, go out and go home by it (crowd.h), the
// shops open and shut by it, and the rival gangs make their rounds by it
// (gang_ai.h). It runs at the speed the player sets (state.speed), and stops
// while a HUD popup is open.

// Seconds of play for an hour of the town at speed 1: a day is 24 minutes,
// 8 at the fast speed.
inline constexpr f32 seconds_per_hour = 60.0f;

// Each fixed step, first: moves the hour (state.hour, state.day) on.
void clock_step(f32 dt);
// How many times faster than speed 1 the town goes now; 0 while paused.
f32 clock_speed();
// Hours since the start of day 1, for when something is due. Setting the
// hour by hand (the T key, the test run) jumps it.
f64 clock_now();
// This step passed midnight / a whole hour.
bool clock_new_day();
bool clock_new_hour();

// Whether the hour `h` is in [from, to); `to` below `from` runs past midnight.
bool hour_between(f32 h, f32 from, f32 to);

// When a kind of business is open, hours of the day; `close` below `open`
// runs past midnight, open == close is open all day.
struct opening {
  f32 open = 0.0f;
  f32 close = 0.0f;
};
opening business_hours(city::business_kind k);
// Whether business `i` (world().businesses) is open at the current hour.
bool business_open(i32 i);
bool business_open_at(i32 i, f32 hour);

} // namespace sandtable
