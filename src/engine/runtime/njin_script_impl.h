#pragma once
#include "njin_internal_only.h"

#include "_mod.h"
#include "njin_script.h"
#include <memory>
#include <string>
#include <vector>

namespace njin {
struct context;
struct script_runtime; // the Lua state and the scripted entities (njin_script.cpp)

// Created on the first script_* call, so a game without scripts pays nothing.
// It sits early in the context, so it is destroyed after every store whose
// callbacks (timers, tweens) may hold references into the Lua state.
struct script_state {
  std::unique_ptr<script_runtime> rt;

  script_state();
  ~script_state();
  script_state(const script_state &) = delete;
  script_state &operator=(const script_state &) = delete;
};

// Script files in use, for the hot reload module to watch.
std::vector<std::string> script_watched_files(const context &ctx);
// Runs a changed file again and swaps its functions into every entity using it.
bool script_reload_file(context &ctx, const std::string &path);

mod_desc script_module();

// Binds the `njin` Lua module (njin_script_bind.cpp).
void script_bind_njin(context &ctx, script_runtime &rt);
} // namespace njin
