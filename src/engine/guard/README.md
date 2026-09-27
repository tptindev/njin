# Engine boundary guard

`njin::api` puts this folder first on the include path of every target that is
not part of the engine itself (a target is part of it when its
`NJIN_ENGINE_INTERNAL` property is on: `njin_rt` and `njin_inspector`). Each
header here has the name of a raylib or GLFW header and only an `#error`, so
`#include <raylib.h>` in a game stops the build, even with a toolchain that
ships its own raylib (w64devkit does).

The other two locks: the private headers of `src/engine/runtime` include
`njin_internal_only.h`, which fails outside the engine, and
`njin_check_boundary()` (`cmake/njin.cmake`) refuses at configure time a game
that links raylib or GLFW, or adds the runtime folder to its include path.
