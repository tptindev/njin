#pragma once
#include "_mod.h"

namespace njin {
// Core module. Draws phase_render through the active camera (camera_active):
// begins 2D mode in phase_pre_render and ends it in phase_post_render, so
// post_render is screen space. Also owns camera_active/w2scr/scr2w.
mod_desc camera_module();
} // namespace njin
