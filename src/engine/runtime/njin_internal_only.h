#pragma once

// Included by every private header of the runtime. NJIN_ENGINE_INTERNAL is
// defined only for engine targets (see src/engine/api/CMakeLists.txt), so a
// game that reaches a runtime header by a relative path stops here.
#ifndef NJIN_ENGINE_INTERNAL
#error "njin: this header is private to the engine runtime. Games include njin.h only."
#endif
