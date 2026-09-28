#pragma once
#include <njin.h>

namespace crowd {
struct options {
  bool gallery = false;     // open on the gallery of every pose
  bool save_sheets = false; // save the baked sheets as PNG to the save folder (what F9 does)
  njin::u32 people = 5000;  // crowd size at start
};

// Thousands of animated 8-direction stick figures, each with its own DNA, who
// walk, run, jump, sit, lie down, greet, shake hands and walk hand in hand;
// drawn with one instanced draw call from sheets baked once at startup.
njin::mod_desc crowd_module(const options &opts = {});
} // namespace crowd
