#pragma once

#include "types.h"

namespace sandtable {

// The rival bosses (every gang but the player's), deciding each hour of the
// town's clock (clock.h) what their men do, from 8:00 to 23:00; at night the
// men stay in. In order:
//
// - collect: a man to each of their shops that is open and owes a day or
//   more of protection;
// - raid the player (CONCEPT.md, "Vòng bảo kê và trả đũa"): now and then,
//   at most once a day, two or three men go to a block of the player's turf
//   and take its shops' money, shop after shop, until they have their quota
//   or meet the player's men; then they go home, and a while later a
//   shopkeeper tells the player (a toast);
// - spread: now and then a man squeezes a shop next to their turf that pays
//   nobody yet.
//
// Each new day they hire a man when they can afford it.

// Each fixed step, after the clock and the men (gang_step).
void gang_ai_step(context &ctx);

} // namespace sandtable
