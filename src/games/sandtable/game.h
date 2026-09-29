#pragma once

#include <njin.h>

namespace sandtable {
using namespace njin;

mod_desc module(bool test_mode = false, i32 test_level = 0, const char *test_plan = nullptr);

} // namespace sandtable
