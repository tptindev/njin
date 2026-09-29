#pragma once
#include <njin.h>

namespace sokoban {
// Box pushing on a grid, drawn in 2.5D with the 3D API: walls and crates are
// boxes, the player is a capsule with its own shader.
njin::mod_desc sokoban_module();
} // namespace sokoban
