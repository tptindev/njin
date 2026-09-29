#pragma once

#include "types.h"

namespace sandtable {

void render_init(context &ctx);
void render_cleanup(context &ctx);
void render_world(context &ctx);
void render_ui(context &ctx);

} // namespace sandtable
