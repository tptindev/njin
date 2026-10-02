#pragma once

#include "types.h"

namespace sandtable {

void render_init(context &ctx);
void render_cleanup(context &ctx);
void render_world(context &ctx);
void render_ui(context &ctx);

// A sheet of men at `at` on the table, each column one motion (person.h)
// from start to end down the rows, drawn while `on`: for the test run to
// look at every animation.
void show_pose_row(bool on, vec2 at = {});
// One-shot eye-view screenshot for --pbk-city-test; runs in the render phase.
void capture_room_camera(i32 house);

} // namespace sandtable
