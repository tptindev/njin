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

// A sheet of men at `at` on the table, each row one motion (walking,
// running, punching, kicking, knocked down, getting up, dying) from start
// to end, drawn over the table while `on`: for the test run to look at
// every animation.
void show_pose_row(bool on, vec2 at = {});

} // namespace sandtable
