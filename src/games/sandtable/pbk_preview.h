#pragma once
#include "types.h"
namespace sandtable {
// The procedural building kit on its own, in three scenes: one Wall and one
// rigged door, the rule package's three example plans, and houses the
// generator makes from seeds. --pbk-preview to look and click; --pbk-test
// runs a script (door clip and collision, screenshots) and quits;
// --pbk-tour plays the same script slower, round and round, for watching.
mod_desc pbk_module(bool test, bool tour = false);
} // namespace sandtable
