#pragma once

#include <njin.h>

namespace sandtable {
using namespace njin;

// `pbk_test`: --pbk-city-test, the town's houses from the procedural building kit.
mod_desc module(bool test_mode = false, u32 seed = 1, bool pbk_test = false);

} // namespace sandtable
