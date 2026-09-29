#pragma once

#include "types.h"

namespace xiangqi {

// The minimap, bottom left, in screen pixels. It is a view of the world, not a
// njin UI panel: render_ui draws it and handle_input pans the camera from it.
inline constexpr rect minimap_area{{16.0f, 520.0f}, {210.0f, 185.0f}};

void render_init(context &ctx);
void render_cleanup(context &ctx);
void render_world(context &ctx);
void render_ui(context &ctx);

} // namespace xiangqi
