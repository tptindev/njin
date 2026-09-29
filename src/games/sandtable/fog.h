#pragma once

#include "types.h"

namespace sandtable {

// Fog of war. The player sees only what their troops see now: round their
// chips and the deployment zone while deploying, round their men in battle.
// Nothing is remembered: ground the troops have left goes back under the fog.

// Works out what is seen this frame. render_world() calls it first.
void fog_update(context &ctx);
bool fog_visible(vec2 pos);
// Covers what is not seen, in world space, over everything else in it.
void fog_draw(context &ctx);

} // namespace sandtable
