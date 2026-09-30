#pragma once

#include "types.h"

namespace sandtable {

void render_init(context &ctx);
void render_cleanup(context &ctx);
void render_world(context &ctx);
void render_ui(context &ctx);

// The item of the open circle menu under the mouse, or -1.
i32 radial_item_at(context &ctx);
// The troop of `owner` whose flag is under the mouse while setting up, or -1.
i32 flag_at(context &ctx, side owner);

} // namespace sandtable
