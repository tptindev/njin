#pragma once
#include <njin.h>

namespace fps {
// Aim trainer in first person: walk on a plane, shoot the red targets of a
// 5x5 grid; each hit moves that target to a free cell.
njin::mod_desc fps_module();
} // namespace fps
